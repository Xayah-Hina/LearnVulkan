#include "renderer_compute_bg.h"
#include "renderer_triangle.h"
#include "renderer_mesh.h"
#include "renderer_barchart.h"
#include "renderer_barchart_font.h"

#include <memory>

std::unique_ptr<IRenderer> CreateDefaultComputeRenderer()
{
    // return std::make_unique<ComputeBackgroundRenderer>();
    // return std::make_unique<TriangleRenderer>();
    // return std::make_unique<MeshRenderer>();
    // return std::make_unique<BarChartRenderer>();
    return std::make_unique<BarChartRendererMSDF>();
}