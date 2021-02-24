#ifndef __NBL_S_OPENGL_STATE_H_INCLUDED__
#define __NBL_S_OPENGL_STATE_H_INCLUDED__

#include "nbl/core/IReferenceCounted.h"
#include "COpenGLBuffer.h"
#include "nbl/video/COpenGLRenderpassIndependentPipeline.h"
#include "nbl/video/COpenGLPipelineLayout.h"
#include "nbl/video/COpenGLDescriptorSet.h"
#include "nbl/video/COpenGLComputePipeline.h"
#include "nbl/video/IOpenGL_FunctionTable.h"
#include "nbl/asset/ECommonEnums.h"
#include <limits>

namespace nbl {
namespace video
{

struct SOpenGLState
{
    static inline constexpr uint32_t MAX_VIEWPORT_COUNT = 16u;

    using buffer_binding_t = asset::SBufferBinding<const COpenGLBuffer>;

    struct SVAO {
        GLuint GLname;
        uint64_t lastUsed;
    };
    struct HashVAOPair
    {
		COpenGLRenderpassIndependentPipeline::SVAOHash first = {};
		SVAO second = { 0u,0ull };
		//extra vao state being cached
		std::array<buffer_binding_t, IGPUMeshBuffer::MAX_ATTR_BUF_BINDING_COUNT> vtxBindings;
        buffer_binding_t idxBinding;
        asset::E_INDEX_TYPE idxType; // not really state but i have to keep it somewhere

        inline bool operator<(const HashVAOPair& rhs) const { return first < rhs.first; }
    };
    struct SDescSetBnd {
        core::smart_refctd_ptr<const COpenGLPipelineLayout> pplnLayout;
        core::smart_refctd_ptr<const COpenGLDescriptorSet> set;
        core::smart_refctd_dynamic_array<uint32_t> dynamicOffsets;
    };

    using SGraphicsPipelineHash = std::array<GLuint, COpenGLRenderpassIndependentPipeline::SHADER_STAGE_COUNT>;

    struct {
        struct {
            core::smart_refctd_ptr<const COpenGLRenderpassIndependentPipeline> pipeline;
            SGraphicsPipelineHash usedShadersHash = { 0u, 0u, 0u, 0u, 0u };
			GLuint usedPipeline = 0u;
        } graphics;
        struct {
            core::smart_refctd_ptr<const COpenGLComputePipeline> pipeline;
            GLuint usedShader = 0u;
        } compute;
    } pipeline;

    struct {
        core::smart_refctd_ptr<const COpenGLBuffer> buffer;
    } dispatchIndirect;

    struct {
        // TODO add viewports to flushing routine
        struct {
            GLint x = 0;
            GLint y = 0;
            // initial values depend on surface attached to context, lets force always updating this (makes life easier, i dont have to query the extents then)
            GLsizei width = std::numeric_limits<GLsizei>::max();
            GLsizei height = std::numeric_limits<GLsizei>::max();

            GLdouble minDepth = 0.0;
            GLdouble maxDepth = 1.0;
        } viewport[MAX_VIEWPORT_COUNT]; // 16 is max supported viewport count throughout implementations
        //in GL it is possible to set polygon mode separately for back- and front-faces, but in VK it's one setting for both
        GLenum polygonMode = GL_FILL;
        GLenum faceCullingEnable = 0;
        GLenum cullFace = GL_BACK;
        //in VK stencil params (both: stencilOp and stencilFunc) are 2 distinct for back- and front-faces, but in GL it's one for both
        struct SStencilOp {
            GLenum sfail = GL_KEEP;
            GLenum dpfail = GL_KEEP;
            GLenum dppass = GL_KEEP;
            bool operator!=(const SStencilOp& rhs) const { return sfail!=rhs.sfail || dpfail!=rhs.dpfail || dppass!=rhs.dppass; }
        };
        SStencilOp stencilOp_front, stencilOp_back;
        struct SStencilFunc {
            GLenum func = GL_ALWAYS;
            GLint ref = 0;
            GLuint mask = ~static_cast<GLuint>(0u);
            bool operator!=(const SStencilFunc& rhs) const { return func!=rhs.func || ref!=rhs.ref || mask!=rhs.mask; }
        };
        SStencilFunc stencilFunc_front, stencilFunc_back;
        // TODO add stencilWriteMask to flushing routine (glStencilMaskSeparate)
        GLuint stencilWriteMask_front;
        GLuint stencilWriteMask_back;
        GLenum depthFunc = GL_LESS;
        GLenum frontFace = GL_CCW;
        GLboolean depthClampEnable = GL_FALSE;
        GLboolean rasterizerDiscardEnable = GL_FALSE;
        GLboolean polygonOffsetEnable = GL_FALSE;
        struct SPolyOffset {
            GLfloat factor = 0.f;//depthBiasSlopeFactor 
            GLfloat units = 0.f;//depthBiasConstantFactor 
            bool operator!=(const SPolyOffset& rhs) const { return factor!=rhs.factor || units!=rhs.units; }
        } polygonOffset;
        GLfloat lineWidth = 1.f;
        GLboolean sampleShadingEnable = GL_FALSE;
        GLfloat minSampleShading = 0.f;
        GLboolean sampleMaskEnable = GL_FALSE;
        GLbitfield sampleMask[2]{~static_cast<GLbitfield>(0), ~static_cast<GLbitfield>(0)};
        GLboolean sampleAlphaToCoverageEnable = GL_FALSE;
        GLboolean sampleAlphaToOneEnable = GL_FALSE;
        GLboolean depthTestEnable = GL_FALSE;
        GLboolean depthWriteEnable = GL_TRUE;
        //GLboolean depthBoundsTestEnable;
        GLboolean stencilTestEnable = GL_FALSE;
        GLboolean multisampleEnable = GL_TRUE;
        GLboolean primitiveRestartEnable = GL_FALSE;

        GLboolean logicOpEnable = GL_FALSE;
        GLenum logicOp = GL_COPY;
        struct SDrawbufferBlending
        {
            GLboolean blendEnable = 0;
            struct SBlendFunc {
                GLenum srcRGB = GL_ONE;
                GLenum dstRGB = GL_ZERO;
                GLenum srcAlpha = GL_ONE;
                GLenum dstAlpha = GL_ZERO;
                bool operator!=(const SBlendFunc& rhs) const { return srcRGB!=rhs.srcRGB || dstRGB!=rhs.dstRGB || srcAlpha!=rhs.srcAlpha || dstAlpha!=rhs.dstAlpha; }
            } blendFunc;
            struct SBlendEq {
                GLenum modeRGB = GL_FUNC_ADD;
                GLenum modeAlpha = GL_FUNC_ADD;
                bool operator!=(const SBlendEq& rhs) const { return modeRGB!=rhs.modeRGB || modeAlpha!=rhs.modeAlpha; }
            } blendEquation;
            struct SColorWritemask {
                GLboolean colorWritemask[4]{ GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE };
                bool operator!=(const SColorWritemask& rhs) const { return memcmp(colorWritemask, rhs.colorWritemask, 4); }
            } colorMask;
        } drawbufferBlend[asset::SBlendParams::MAX_COLOR_ATTACHMENT_COUNT];
    } rasterParams;

    struct {
		HashVAOPair vao = {};

        //putting it here because idk where else
        core::smart_refctd_ptr<const COpenGLBuffer> indirectDrawBuf;
        core::smart_refctd_ptr<const COpenGLBuffer> parameterBuf;//GL>=4.6
    } vertexInputParams;

    struct {
        SDescSetBnd descSets[IGPUPipelineLayout::DESCRIPTOR_SET_COUNT];
    } descriptorsParams[asset::E_PIPELINE_BIND_POINT::EPBP_COUNT];

    struct SPixelPackUnpack {
        core::smart_refctd_ptr<const COpenGLBuffer> buffer;
        GLint alignment = 4;
        GLint rowLength = 0;
        GLint imgHeight = 0;
        GLint BCwidth = 0;
        GLint BCheight = 0;
        GLint BCdepth = 0;
    };
    SPixelPackUnpack pixelPack;
    SPixelPackUnpack pixelUnpack;
};

}
}

#endif