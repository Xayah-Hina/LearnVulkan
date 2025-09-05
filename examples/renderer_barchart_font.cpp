#include "renderer_barchart_font.h"
#include "src/ext/vk_initializers.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <stdexcept>

#include "stb_image.h"

#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult err__=(x); if(err__!=VK_SUCCESS){ throw std::runtime_error("Vulkan error "+std::to_string(err__)); } } while(0)
#endif

// ===== 工具：读文件 =====
static std::vector<char> read_bin(const char* p){
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if(!f) throw std::runtime_error(std::string("open file failed: ")+p);
    auto n=(size_t)f.tellg(); std::vector<char> b(n);
    f.seekg(0); f.read(b.data(),n); return b;
}
static std::string read_txt(const std::string& p){
    std::ifstream f(p); if(!f) throw std::runtime_error("open file failed: "+p);
    std::ostringstream ss; ss<<f.rdbuf(); return ss.str();
}
static VkShaderModule create_shader(VkDevice d, const std::vector<char>& code){
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize=code.size(); ci.pCode=reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(d,&ci,nullptr,&m)); return m;
}

// ===== IRenderer 接口 =====

void BarChartRendererMSDF::initialize(const RenderContext& ctx){
    // 默认路径：使用 CMake 生成产物
    if(atlas_png_.empty())  atlas_png_  = "assets/atlas_digits.png";
    if(atlas_json_.empty()) atlas_json_ = "assets/atlas_digits.json";

    create_bar_pipeline(ctx);
    create_bar_descriptors(ctx);

    create_text_pipeline(ctx);
    load_msdf_atlas(ctx);        // 读取 PNG + 上传 + 创建 sampler/view
    parse_msdf_json();           // 解析 0..9 的 uv
    ensure_glyph_ssbo(ctx);      // 建 SSBO
    create_text_descriptors(ctx);
}

void BarChartRendererMSDF::destroy(const RenderContext& ctx){
    destroy_text_pipeline(ctx.device);
    destroy_bar_pipeline(ctx.device);
    destroy_glyph_ssbo(ctx.device, ctx.allocator);
    destroy_font_resources(ctx.device, ctx.allocator);
}

void BarChartRendererMSDF::on_swapchain_resized(const RenderContext& ctx){
    // offscreen view 没变（引擎维护），只需重写 bar 的 descriptor 指向新的 view
    create_bar_descriptors(ctx);
}

void BarChartRendererMSDF::record(VkCommandBuffer cmd, uint32_t W, uint32_t H, const RenderContext& ctx){
    // 1) offscreen → GENERAL，写柱子
    transition_image(cmd, ctx.offscreenImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, 0,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    // 绑定柱状图 compute
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bar_.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bar_.layout, 0, 1, &bar_.dset, 0, nullptr);

    struct PCBar{
        uint32_t W,H; float margin_px,gap_px,base_line_px,max_value;
    } pcBar{W,H, params_.margin_px, params_.gap_px, params_.base_line_px, params_.max_value};

    vkCmdPushConstants(cmd, bar_.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PCBar), &pcBar);

    uint32_t gx=(W+15)/16, gy=(H+15)/16;
    vkCmdDispatch(cmd, gx, gy, 1);

    // 2) 同一张 offscreen 上叠加 MSDF 文字
    // 准备 glyph 实例（基于柱子几何）
    build_digits_for_bars(W,H,ctx);

    // atlas 维持在 SHADER_READ_ONLY_OPTIMAL，无需改布局
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, text_.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, text_.layout, 0, 1, &text_.dset, 0, nullptr);

    struct PCText{ uint32_t W,H; float pxRange, gamma; } pcT{W,H, params_.pxRange, 2.2f};
    vkCmdPushConstants(cmd, text_.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PCText), &pcT);

    vkCmdDispatch(cmd, gx, gy, 1);

    // 3) offscreen → TRANSFER_SRC，swapchain → TRANSFER_DST，blit
    transition_image(cmd, ctx.offscreenImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    transition_image(cmd, ctx.swapchainImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    copy_offscreen_to_swapchain(cmd, ctx.offscreenImage, ctx.swapchainImage, ctx.frameExtent);
}

// ===== 资源：柱状图 =====

void BarChartRendererMSDF::create_bar_pipeline(const RenderContext& ctx){
    // dsl: binding0 = storage image
    VkDescriptorSetLayoutBinding b0{};
    b0.binding=0; b0.descriptorCount=1; b0.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b0.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount=1; dslci.pBindings=&b0;
    VK_CHECK(vkCreateDescriptorSetLayout(ctx.device,&dslci,nullptr,&bar_.dsl));

    VkPushConstantRange pcr{};
    pcr.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset=0;
    pcr.size=sizeof(uint32_t)*2 + sizeof(float)*4;

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount=1; plci.pSetLayouts=&bar_.dsl; plci.pushConstantRangeCount=1; plci.pPushConstantRanges=&pcr;
    VK_CHECK(vkCreatePipelineLayout(ctx.device,&plci,nullptr,&bar_.layout));

    auto code=read_bin("shaders/barchart.comp.spv");
    bar_.cs=create_shader(ctx.device, code);

    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage=VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,
        VK_SHADER_STAGE_COMPUTE_BIT, bar_.cs, "main", nullptr};
    cpci.layout=bar_.layout;
    VK_CHECK(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &bar_.pipeline));
}

void BarChartRendererMSDF::create_bar_descriptors(const RenderContext& ctx){
    if(bar_.dset){} // 会被新的覆盖
    bar_.dset = ctx.descriptorAllocator->allocate(ctx.device, bar_.dsl);

    VkDescriptorImageInfo ii{}; ii.imageView=ctx.offscreenImageView; ii.imageLayout=VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet=bar_.dset; w.dstBinding=0; w.descriptorCount=1; w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo=&ii;
    vkUpdateDescriptorSets(ctx.device,1,&w,0,nullptr);
}

void BarChartRendererMSDF::destroy_bar_pipeline(VkDevice d){
    if(bar_.pipeline){ vkDestroyPipeline(d,bar_.pipeline,nullptr); bar_.pipeline=VK_NULL_HANDLE; }
    if(bar_.cs){ vkDestroyShaderModule(d,bar_.cs,nullptr); bar_.cs=VK_NULL_HANDLE; }
    if(bar_.layout){ vkDestroyPipelineLayout(d,bar_.layout,nullptr); bar_.layout=VK_NULL_HANDLE; }
    if(bar_.dsl){ vkDestroyDescriptorSetLayout(d,bar_.dsl,nullptr); bar_.dsl=VK_NULL_HANDLE; }
}

// ===== 资源：文字管线 + 字体图集 + SSBO =====

void BarChartRendererMSDF::create_text_pipeline(const RenderContext& ctx){
    // dsl: b0 storage image, b1 combined sampler, b2 storage buffer
    VkDescriptorSetLayoutBinding b0{}; b0.binding=0; b0.descriptorCount=1; b0.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; b0.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding b1{}; b1.binding=1; b1.descriptorCount=1; b1.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b1.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding b2{}; b2.binding=2; b2.descriptorCount=1; b2.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; b2.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    std::array<VkDescriptorSetLayoutBinding,3> binds{b0,b1,b2};

    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount=(uint32_t)binds.size(); dslci.pBindings=binds.data();
    VK_CHECK(vkCreateDescriptorSetLayout(ctx.device,&dslci,nullptr,&text_.dsl));

    VkPushConstantRange pcr{}; pcr.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset=0; pcr.size=sizeof(uint32_t)*2 + sizeof(float)*2;
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount=1; plci.pSetLayouts=&text_.dsl; plci.pushConstantRangeCount=1; plci.pPushConstantRanges=&pcr;
    VK_CHECK(vkCreatePipelineLayout(ctx.device,&plci,nullptr,&text_.layout));

    auto code=read_bin("shaders/barchart_font.comp.spv");
    text_.cs=create_shader(ctx.device, code);

    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage=VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,
        VK_SHADER_STAGE_COMPUTE_BIT, text_.cs, "main", nullptr};
    cpci.layout=text_.layout;
    VK_CHECK(vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &text_.pipeline));
}

void BarChartRendererMSDF::ensure_glyph_ssbo(const RenderContext& ctx){
    if(glyph_buf_) return;
    VkDeviceSize cap = sizeof(float)* (2+2+4+4) * glyph_cap_; // 按 Glyph 估算
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = cap; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{}; ai.usage = VMA_MEMORY_USAGE_AUTO; ai.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    VK_CHECK(vmaCreateBuffer(ctx.allocator, &bi, &ai, &glyph_buf_, &glyph_alloc_, nullptr));
}

void BarChartRendererMSDF::create_text_descriptors(const RenderContext& ctx){
    text_.dset = ctx.descriptorAllocator->allocate(ctx.device, text_.dsl);

    VkDescriptorImageInfo img{};
    img.sampler = atlas_sampler_; img.imageView = atlas_view_; img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo buf{};
    buf.buffer = glyph_buf_; buf.offset=0; buf.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet,2> ws{};
    // b1 sampler
    ws[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr, text_.dset, 1, 0, 1,
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &img, nullptr, nullptr};
    // b2 ssbo
    ws[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr, text_.dset, 2, 0, 1,
                                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buf, nullptr};

    // 还要把 b0=storage image 也补上，沿用 offscreen
    VkDescriptorImageInfo ii{}; ii.imageView = ctx.offscreenImageView; ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w0.dstSet = text_.dset; w0.dstBinding = 0; w0.descriptorCount=1; w0.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w0.pImageInfo=&ii;

    std::array<VkWriteDescriptorSet,3> all{w0, ws[0], ws[1]};
    vkUpdateDescriptorSets(ctx.device, (uint32_t)all.size(), all.data(), 0, nullptr);
}

void BarChartRendererMSDF::destroy_text_pipeline(VkDevice d){
    if(text_.pipeline){ vkDestroyPipeline(d,text_.pipeline,nullptr); text_.pipeline=VK_NULL_HANDLE; }
    if(text_.cs){ vkDestroyShaderModule(d,text_.cs,nullptr); text_.cs=VK_NULL_HANDLE; }
    if(text_.layout){ vkDestroyPipelineLayout(d,text_.layout,nullptr); text_.layout=VK_NULL_HANDLE; }
    if(text_.dsl){ vkDestroyDescriptorSetLayout(d,text_.dsl,nullptr); text_.dsl=VK_NULL_HANDLE; }
}

void BarChartRendererMSDF::destroy_font_resources(VkDevice d, VmaAllocator a){
    if(atlas_view_){ vkDestroyImageView(d, atlas_view_, nullptr); atlas_view_={}; }
    if(atlas_sampler_){ vkDestroySampler(d, atlas_sampler_, nullptr); atlas_sampler_={}; }
    if(atlas_image_){ vmaDestroyImage(a, atlas_image_, atlas_alloc_); atlas_image_={}; atlas_alloc_={}; }
}
void BarChartRendererMSDF::destroy_glyph_ssbo(VkDevice d, VmaAllocator a){
    if(glyph_buf_){ vmaDestroyBuffer(a, glyph_buf_, glyph_alloc_); glyph_buf_={}; glyph_alloc_={}; }
}

// ===== 读取 PNG 并上传，解析 JSON =====

void BarChartRendererMSDF::load_msdf_atlas(const RenderContext& ctx){
    int w,h,c; stbi_uc* data = stbi_load(atlas_png_.c_str(), &w,&h,&c, 4);
    if(!data) throw std::runtime_error("stbi_load atlas failed: "+atlas_png_);
    atlas_w_ = (uint32_t)w; atlas_h_=(uint32_t)h;

    // staging buffer
    VkDeviceSize bytes = (VkDeviceSize)w*h*4;
    VkBuffer staging{}; VmaAllocation stagingAlloc{};
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size=bytes; bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo ai{}; ai.usage=VMA_MEMORY_USAGE_AUTO; ai.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    VK_CHECK(vmaCreateBuffer(ctx.allocator,&bi,&ai,&staging,&stagingAlloc,nullptr));

    void* mapped{}; vmaMapMemory(ctx.allocator, stagingAlloc, &mapped);
    memcpy(mapped,data,(size_t)bytes);
    vmaUnmapMemory(ctx.allocator, stagingAlloc);
    stbi_image_free(data);

    // GPU image
    VkExtent3D extent{(uint32_t)w,(uint32_t)h,1};
    VkImageCreateInfo ici = vkinit::image_create_info(VK_FORMAT_R8G8B8A8_UNORM,
                                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
                                                      extent);
    VmaAllocationCreateInfo iai{}; iai.usage=VMA_MEMORY_USAGE_AUTO; iai.flags=VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    VK_CHECK(vmaCreateImage(ctx.allocator,&ici,&iai,&atlas_image_,&atlas_alloc_,nullptr));

    VkImageViewCreateInfo vci = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, atlas_image_, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(ctx.device,&vci,nullptr,&atlas_view_));

    // sampler
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter=VK_FILTER_LINEAR; sci.minFilter=VK_FILTER_LINEAR;
    sci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(ctx.device,&sci,nullptr,&atlas_sampler_));

    // one-shot 命令上传
    VkCommandPool pool{};
    VkCommandPoolCreateInfo pci = vkinit::command_pool_create_info(ctx.graphics_queue_family, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    VK_CHECK(vkCreateCommandPool(ctx.device,&pci,nullptr,&pool));
    VkCommandBufferAllocateInfo cbai = vkinit::command_buffer_allocate_info(pool,1);
    VkCommandBuffer cmd; VK_CHECK(vkAllocateCommandBuffers(ctx.device,&cbai,&cmd));

    VkCommandBufferBeginInfo bi2 = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd,&bi2));

    // layout → DST
    transition_image(cmd, atlas_image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy2 region{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
    region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; region.imageSubresource.mipLevel=0;
    region.imageSubresource.baseArrayLayer=0; region.imageSubresource.layerCount=1;
    region.imageExtent=extent;

    VkCopyBufferToImageInfo2 ci2{VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2};
    ci2.srcBuffer=staging; ci2.dstImage=atlas_image_; ci2.dstImageLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ci2.regionCount=1; ci2.pRegions=&region;
    vkCmdCopyBufferToImage2(cmd,&ci2);

    // → SAMPLED
    transition_image(cmd, atlas_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    VK_CHECK(vkEndCommandBuffer(cmd));
    VkCommandBufferSubmitInfo cbsi = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 si = vkinit::submit_info(&cbsi, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(ctx.graphics_queue,1,&si,VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(ctx.graphics_queue));

    vkDestroyCommandPool(ctx.device,pool,nullptr);
    vmaDestroyBuffer(ctx.allocator, staging, stagingAlloc);
}

void BarChartRendererMSDF::parse_msdf_json(){
    // 只解析 0..9 的 atlasBounds
    std::string s = read_txt(atlas_json_);
    for(int d=0; d<=9; ++d){
        int code = '0'+d; // 48..57
        // 找到 "unicode": <code>
        std::string ukey = "\"unicode\"";
        size_t pos = s.find(ukey);
        size_t last = 0;
        bool found=false;
        while(pos!=std::string::npos){
            size_t colon = s.find(':', pos);
            size_t comma = s.find_first_of(",}", colon+1);
            int val = std::stoi(s.substr(colon+1, comma-colon-1));
            if(val==code){
                // 在这个条目附近寻找 atlasBounds
                size_t ab = s.find("\"atlasBounds\"", comma);
                size_t lb = s.find('{', ab);
                size_t rb = s.find('}', lb);
                std::string block = s.substr(lb, rb-lb+1);
                // 提取 left,bottom,right,top（msdf-atlas-gen 的 JSON 键）
                auto pick=[&](const char* key)->float{
                    size_t p=block.find(key);
                    size_t c=block.find(':',p);
                    size_t e=block.find_first_of(",}",c+1);
                    return std::stof(block.substr(c+1, e-c-1));
                };
                float left  = pick("\"left\"");
                float bottom= pick("\"bottom\"");
                float right = pick("\"right\"");
                float top   = pick("\"top\"");
                // 你生成时用的是 -yorigin top。我们把 UV 直接按 xy=左上，zw=右下 组织
                // 注意：如果发现渲染颠倒，把 v 做一次 1-y 翻转即可
                uv_digits_[d] = UvRect{
                    left / float(atlas_w_),
                    top  / float(atlas_h_),
                    right/ float(atlas_w_),
                    bottom/float(atlas_h_)
                };
                found=true;
                break;
            }
            last = comma;
            pos = s.find(ukey, last);
        }
        if(!found) throw std::runtime_error("digit not found in json: "+std::to_string(d));
    }
}

// ===== 每帧构建 glyph 实例 =====

void BarChartRendererMSDF::build_digits_for_bars(uint32_t W, uint32_t H, const RenderContext& ctx){
    struct GlyphCPU { float px,py,sx,sy,u0,v0,u1,v1,r,g,b,a; };
    std::vector<GlyphCPU> gs;
    gs.reserve(32);

    // 与 shader 一致的柱体几何
    const uint32_t N=11;
    float axis_y = float(H) - params_.base_line_px;
    float innerW = float(W) - 2.0f*params_.margin_px;
    float innerH = float(H) - params_.margin_px - params_.base_line_px;
    float slotW  = innerW / float(N);
    float barW   = std::max(1.0f, slotW - params_.gap_px);

    auto addDigit=[&](int d, float xc, float yTop){
        float Hlbl = params_.label_px;
        float Wlbl = Hlbl * 0.6f;
        float posx = xc - 0.5f*Wlbl;
        float posy = yTop - params_.label_gap_px - Hlbl;
        UvRect uv = uv_digits_[d];
        gs.push_back(GlyphCPU{posx,posy, Wlbl,Hlbl, uv.u0,uv.v0,uv.u1,uv.v1, 0.98f,0.98f,1.0f,1.0f});
    };

    for(uint32_t i=0;i<N;++i){
        float v = float(i);
        float h = innerH * std::min(std::max(v/params_.max_value,0.0f),1.0f);
        float x0 = params_.margin_px + float(i)*slotW + params_.gap_px*0.5f;
        float x1 = x0 + barW;
        float yTop = axis_y - h;
        float xc = 0.5f*(x0+x1);

        if(i < 10) addDigit(int(i), xc, yTop);
        else{
            // “10” 两位数
            float Hlbl=params_.label_px, Wlbl=Hlbl*0.6f, spacing=Hlbl*0.1f;
            float total = Wlbl*2.0f + spacing;
            float left = xc - 0.5f*total;
            UvRect u0=uv_digits_[1], u1=uv_digits_[0];
            gs.push_back(GlyphCPU{left, yTop - params_.label_gap_px - Hlbl, Wlbl,Hlbl, u0.u0,u0.v0,u0.u1,u0.v1, 0.98f,0.98f,1.0f,1.0f});
            gs.push_back(GlyphCPU{left+Wlbl+spacing, yTop - params_.label_gap_px - Hlbl, Wlbl,Hlbl, u1.u0,u1.v0,u1.u1,u1.v1, 0.98f,0.98f,1.0f,1.0f});
        }
    }

    // 写入 SSBO
    VkDeviceSize bytes = gs.size()*sizeof(GlyphCPU);
    // 用一个小 staging 写入（也可以直接建 HOST 可见 SSBO）
    VkBuffer staging{}; VmaAllocation stagingAlloc{};
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size=bytes; bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo ai{}; ai.usage=VMA_MEMORY_USAGE_AUTO; ai.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    VK_CHECK(vmaCreateBuffer(ctx.allocator,&bi,&ai,&staging,&stagingAlloc,nullptr));
    void* mp{}; vmaMapMemory(ctx.allocator, stagingAlloc, &mp); memcpy(mp, gs.data(), (size_t)bytes); vmaUnmapMemory(ctx.allocator, stagingAlloc);

    // 目标 SSBO 若还没创建就建一个（已在 initialize 中保底创建）
    ensure_glyph_ssbo(ctx);

    // 复制 staging → glyph_buf_
    // 用一次性 cmd
    // 这里直接借用图形队列（和绘制同队列），可安全按帧写入
    VkCommandPool pool{};
    VkCommandPoolCreateInfo pci = vkinit::command_pool_create_info(ctx.graphics_queue_family, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    VK_CHECK(vkCreateCommandPool(ctx.device,&pci,nullptr,&pool));
    VkCommandBufferAllocateInfo cbai = vkinit::command_buffer_allocate_info(pool,1);
    VkCommandBuffer cmd; VK_CHECK(vkAllocateCommandBuffers(ctx.device,&cbai,&cmd));
    VkCommandBufferBeginInfo bi2 = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd,&bi2));
    VkBufferCopy region{0,0,bytes};
    vkCmdCopyBuffer(cmd, staging, glyph_buf_, 1, &region);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkCommandBufferSubmitInfo cbsi = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 si = vkinit::submit_info(&cbsi,nullptr,nullptr);
    VK_CHECK(vkQueueSubmit2(ctx.graphics_queue,1,&si,VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(ctx.graphics_queue));
    vkDestroyCommandPool(ctx.device,pool,nullptr);
    vmaDestroyBuffer(ctx.allocator, staging, stagingAlloc);

    // 更新 descriptor 不需要改，因为 glyph_buf_ 没换
}

// ===== 同步工具 =====

void BarChartRendererMSDF::transition_image(VkCommandBuffer cmd, VkImage img,
                          VkImageLayout oldL, VkImageLayout newL,
                          VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
                          VkAccessFlags2 srcAcc, VkAccessFlags2 dstAcc){
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask=src; b.dstStageMask=dst; b.srcAccessMask=srcAcc; b.dstAccessMask=dstAcc;
    b.oldLayout=oldL; b.newLayout=newL; b.image=img;
    b.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; b.subresourceRange.levelCount=1; b.subresourceRange.layerCount=1;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dep.imageMemoryBarrierCount=1; dep.pImageMemoryBarriers=&b;
    vkCmdPipelineBarrier2(cmd,&dep);
}
void BarChartRendererMSDF::copy_offscreen_to_swapchain(VkCommandBuffer cmd, VkImage src, VkImage dst, VkExtent2D extent){
    VkImageBlit2 blit{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
    blit.srcSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; blit.srcSubresource.layerCount=1;
    blit.srcOffsets[0]={0,0,0}; blit.srcOffsets[1]={(int32_t)extent.width,(int32_t)extent.height,1};
    blit.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; blit.dstSubresource.layerCount=1;
    blit.dstOffsets[0]={0,0,0}; blit.dstOffsets[1]={(int32_t)extent.width,(int32_t)extent.height,1};
    VkBlitImageInfo2 info{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
    info.srcImage=src; info.srcImageLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    info.dstImage=dst; info.dstImageLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; info.regionCount=1; info.pRegions=&blit;
    info.filter=VK_FILTER_LINEAR;
    vkCmdBlitImage2(cmd,&info);
}
