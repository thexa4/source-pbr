//===================== Copyright (c) Valve Corporation. All Rights Reserved. ======================
//
// Physically Based Rendering shader for brushes and models
//
//==================================================================================================

// includes for all shaders
#include "BaseVSShader.h"
#include "cpp_shader_constant_register_map.h"

// includes specific to this shader
#include "pbr_vs20.inc"
#include "pbr_ps30.inc"

static ConVar mat_fullbright("mat_fullbright", "0", FCVAR_CHEAT);
static ConVar mat_specular("mat_specular", "1", FCVAR_CHEAT);

struct PBR_DX9_Vars_t
{
    PBR_DX9_Vars_t() { memset(this, 0xFF, sizeof(*this)); }

    int Albedo;
    int Normal;
    int Mrao;
    int Emissive;
    int Envmap;
    int AlphaTestReference;
    int BaseTextureFrame;
    int BaseTextureTransform;
    int FlashlightTexture;
    int FlashlightTextureFrame;
};

DEFINE_FALLBACK_SHADER(PBR, PBR_DX9)
BEGIN_VS_SHADER(PBR_DX9, "Physically Based Rendering shader for brushes and models")

BEGIN_SHADER_PARAMS
SHADER_PARAM(ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.0", "Alpha Test Refernce")
SHADER_PARAM(BASETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Albedo Texture")      // Changing this name breaks basetexturetransform
SHADER_PARAM(BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Bump Map")                // Changing this name breaks dynamic lighting
SHADER_PARAM(NORMALTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Normal Map")        // This is here for backwards compatibility
SHADER_PARAM(MRAOTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Metal/Rough/AO Map")  // Changing this name breaks backwards compatibility
SHADER_PARAM(EMISSIVE, SHADER_PARAM_TYPE_TEXTURE, "", "Emissive Texture")       // You could probably get away with changing this name
SHADER_PARAM(ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Environment Map")          // Don't try it for this though
END_SHADER_PARAMS

void SetupVars(PBR_DX9_Vars_t &info)
{
    info.AlphaTestReference = ALPHATESTREFERENCE;
    info.Albedo = BASETEXTURE;
    info.Normal = BUMPMAP;
    info.Mrao = MRAOTEXTURE;
    info.Emissive = EMISSIVE;
    info.Envmap = ENVMAP;
    info.BaseTextureFrame = FRAME;
    info.BaseTextureTransform = BASETEXTURETRANSFORM;
    info.FlashlightTexture = FLASHLIGHTTEXTURE;
    info.FlashlightTextureFrame = FLASHLIGHTTEXTUREFRAME;
}

SHADER_INIT_PARAMS()
{
    PBR_DX9_Vars_t info;
    SetupVars(info);

    // This is here for backwards compatibility
    if (Q_strcmp(params[NORMALTEXTURE]->GetStringValue(), "") != 0)
    {
        params[BUMPMAP]->SetStringValue(params[NORMALTEXTURE]->GetStringValue());
    }

    // Without this, dynamic lighting breaks
    if (Q_strcmp(params[BUMPMAP]->GetStringValue(), "") == 0)
    {
        params[BUMPMAP]->SetStringValue("dev/flat_normal");
    }

    // Check that the Flashlight Texture was set
    // Using assert because if this isn't set there's something seriously wrong
    Assert(info.FlashlightTexture >= 0);

    // Check if the hardware supports Flashlight Border Color
    if (g_pHardwareConfig->SupportsBorderColor())
    {
        params[FLASHLIGHTTEXTURE]->SetStringValue("effects/flashlight_border");
    }
    else
    {
        params[FLASHLIGHTTEXTURE]->SetStringValue("effects/flashlight001");
    }

    // Set material var2 flags
    if (IS_FLAG_SET(MATERIAL_VAR_MODEL))
    {
        SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_HW_SKINNING);           // Required for skinning
        SET_FLAGS2(MATERIAL_VAR2_LIGHTING_VERTEX_LIT);            // Required for dynamic lighting
        SET_FLAGS2(MATERIAL_VAR2_NEEDS_TANGENT_SPACES);           // Required for dynamic lighting
        SET_FLAGS2(MATERIAL_VAR2_NEEDS_BAKED_LIGHTING_SNAPSHOTS); // Required for ambient cube
    }
    else // Lightmapped
    {
        SET_FLAGS2(MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP);       // Required for lightmaps
        SET_FLAGS2(MATERIAL_VAR2_NEEDS_TANGENT_SPACES);           // Required for dynamic lighting
        SET_FLAGS2(MATERIAL_VAR2_NEEDS_BAKED_LIGHTING_SNAPSHOTS); // Required for ambient cube
    }
}

SHADER_FALLBACK
{
    return 0;
}

SHADER_INIT
{
    PBR_DX9_Vars_t info;
    SetupVars(info);

    // Setting up Albedo Texture
    bool bIsAlbedoTranslucent = false;
    if (params[info.Albedo]->IsDefined())
    {
        this->LoadTexture(info.Albedo, TEXTUREFLAGS_SRGB); // Albedo is sRGB

        if (params[info.Albedo]->GetTextureValue()->IsTranslucent())
        {
            bIsAlbedoTranslucent = true;
        }
    }

    // Setting up Normal Map
    if (info.Normal != -1 && params[info.Normal]->IsDefined())
    {
        this->LoadBumpMap(info.Normal); // Normal is a bump map
    }

    // Setting up MRAO Map
    if (info.Mrao != -1 && params[info.Mrao]->IsDefined())
    {
        this->LoadTexture(info.Mrao);
    }

    // Setting up Emissive Texture
    if (info.Emissive != -1 && params[info.Emissive]->IsDefined())
    {
        this->LoadTexture(info.Emissive, TEXTUREFLAGS_SRGB); // Emissive is sRGB
    }

    // Setting up Environment Map
    if (info.Envmap != -1 && params[info.Envmap]->IsDefined())
    {
        // Theoretically this should allow the game to access lower mips, but it doesn't seem to work
        int flags = TEXTUREFLAGS_ALL_MIPS | TEXTUREFLAGS_NOLOD;
        if (g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE)
        {
            flags |= TEXTUREFLAGS_SRGB; // Envmap is only sRGB with HDR disabled?
        }

        // If the hardware doesn't support cubemaps, use spheremaps instead
        if (!g_pHardwareConfig->SupportsCubeMaps())
        {
            SET_FLAGS(MATERIAL_VAR_ENVMAPSPHERE);
        }

        // Done like this so the user could set $envmapsphere themselves if they wanted to
        if (!IS_FLAG_SET(MATERIAL_VAR_ENVMAPSPHERE))
        {
            this->LoadCubeMap(info.Envmap, flags);
        }
        else
        {
            this->LoadTexture(info.Envmap, flags);
        }
    }

    // Setting up Flashlight Texture
    Assert(info.FlashlightTexture >= 0);                          // Assert for Flashlight stuff cause idk
    this->LoadTexture(info.FlashlightTexture, TEXTUREFLAGS_SRGB); // Flashlight Texture is sRGB
};

SHADER_DRAW
{
    PBR_DX9_Vars_t info;
    SetupVars(info);

    bool bIsAlphaTested = IS_FLAG_SET(MATERIAL_VAR_ALPHATEST) != 0;
    bool bHasAlbedo = (info.Albedo != -1) && params[info.Albedo]->IsTexture();
    bool bHasNormal = (info.Normal != -1) && params[info.Normal]->IsTexture();
    bool bHasMrao = (info.Mrao != -1) && params[info.Mrao]->IsTexture();
    bool bHasEmissive = (info.Emissive != -1) && params[info.Emissive]->IsTexture();
    bool bHasEnvmap = (info.Envmap != -1) && params[info.Envmap]->IsTexture();
    bool bHasFlashlight = this->UsingFlashlight(params);
    bool bLightmapped = !IS_FLAG_SET(MATERIAL_VAR_MODEL);

    BlendType_t nBlendType = this->EvaluateBlendRequirements(info.Albedo, true);
    bool bFullyOpaque = (nBlendType != BT_BLENDADD) && (nBlendType != BT_BLEND) && !bIsAlphaTested;

    if (this->IsSnapshotting())
    {
        pShaderShadow->EnableAlphaTest(bIsAlphaTested);

        if (info.AlphaTestReference != -1 && params[info.AlphaTestReference]->GetFloatValue() > 0.0f)
        {
            pShaderShadow->AlphaFunc(SHADER_ALPHAFUNC_GEQUAL, params[info.AlphaTestReference]->GetFloatValue());
        }

        int nShadowFilterMode = bHasFlashlight ? g_pHardwareConfig->GetShadowFilterMode() : 0;

        if (params[info.Albedo]->IsTexture())
        {
            this->SetDefaultBlendingShadowState(info.Albedo, true);
        }

        pShaderShadow->EnableTexture(SHADER_SAMPLER0, true);  // Albedo Texture
        pShaderShadow->EnableSRGBRead(SHADER_SAMPLER0, true); // Albedo is sRGB

        pShaderShadow->EnableTexture(SHADER_SAMPLER1, true); // Normal map

        pShaderShadow->EnableTexture(SHADER_SAMPLER2, true); // MRAO Map

        pShaderShadow->EnableTexture(SHADER_SAMPLER3, true);  // Emissive Texture
        pShaderShadow->EnableSRGBRead(SHADER_SAMPLER3, true); // Emissive is sRGB

        // Set up Envmap
        if (bHasEnvmap)
        {
            pShaderShadow->EnableTexture(SHADER_SAMPLER4, true); // Envmap
            if (g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE)
            {
                pShaderShadow->EnableSRGBRead(SHADER_SAMPLER4, true); // Envmap is sRGB but only if there's no HDR
            }
        }

        // If the flashlight is on and hitting this model/brush, set up its textures
        if (bHasFlashlight)
        {
            pShaderShadow->EnableTexture(SHADER_SAMPLER5, true);     // Flashlight Cookie
            pShaderShadow->EnableSRGBRead(SHADER_SAMPLER5, true);    // Flashlight Cookie is sRGB
            pShaderShadow->EnableTexture(SHADER_SAMPLER6, true);     // Shadow Depth Map
            pShaderShadow->SetShadowDepthFiltering(SHADER_SAMPLER6); // Set Shadow Depth Filtering
            pShaderShadow->EnableSRGBRead(SHADER_SAMPLER6, false);   // Shadow Depth Map is not sRGB
            pShaderShadow->EnableTexture(SHADER_SAMPLER7, true);     // Random Rotation Map
        }

        if (bLightmapped)
        {
            pShaderShadow->EnableTexture(SHADER_SAMPLER8, true); // Lightmap
        }

        // Enabling sRGB writing, because without it, the output is very dark
        // https://github.com/thexa4/source-pbr/blob/feature/pbr-base/mp/src/materialsystem/stdshaders/common_ps_fxc.h#L349
        // Because ps2b shaders and up do write sRGB
        pShaderShadow->EnableSRGBWrite(true);

        // Set up vertex format
        if (bLightmapped) // brush
        {
            unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL;
            pShaderShadow->VertexShaderVertexFormat(flags, 3, 0, 0);
        }
        else // model
        {
            unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
            pShaderShadow->VertexShaderVertexFormat(flags, 1, 0, 0);
        }

        DECLARE_STATIC_VERTEX_SHADER(pbr_vs20);
        SET_STATIC_VERTEX_SHADER_COMBO(LIGHTMAPPED, bLightmapped);
        SET_STATIC_VERTEX_SHADER(pbr_vs20);

        DECLARE_STATIC_PIXEL_SHADER(pbr_ps30);
        SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHT, bHasFlashlight);
        SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHTDEPTHFILTERMODE, nShadowFilterMode);
        SET_STATIC_PIXEL_SHADER_COMBO(LIGHTMAPPED, bLightmapped);
        SET_STATIC_PIXEL_SHADER(pbr_ps30);

        this->DefaultFog();

        // Enable alpha writes all the time so that we have them for underwater stuff
        pShaderShadow->EnableAlphaWrites(bFullyOpaque);
    }
    else // not snapshotting - begin dynamic state
    {
        if (bHasAlbedo)
        {
            this->BindTexture(SHADER_SAMPLER0, info.Albedo, info.BaseTextureFrame);
        }
        else
        {
            pShaderAPI->BindStandardTexture(SHADER_SAMPLER0, TEXTURE_WHITE);
        }

        if (bHasNormal)
        {
            this->BindTexture(SHADER_SAMPLER1, info.Normal);
        }
        else
        {
            pShaderAPI->BindStandardTexture(SHADER_SAMPLER1, TEXTURE_NORMALMAP_FLAT);
        }

        if (bHasMrao)
        {
            this->BindTexture(SHADER_SAMPLER2, info.Mrao);
        }
        else
        {
            pShaderAPI->BindStandardTexture(SHADER_SAMPLER2, TEXTURE_WHITE);
        }

        if (bHasEmissive)
        {
            this->BindTexture(SHADER_SAMPLER3, info.Emissive);
        }
        else
        {
            pShaderAPI->BindStandardTexture(SHADER_SAMPLER3, TEXTURE_BLACK);
        }

        if (bHasEnvmap)
        {
            this->BindTexture(SHADER_SAMPLER4, info.Envmap);
        }
        else
        {
            pShaderAPI->BindStandardTexture(SHADER_SAMPLER4, TEXTURE_GREY);
        }

        LightState_t lightState;
        pShaderAPI->GetDX9LightState(&lightState);

        bool bFlashlightShadows = false;
        if (bHasFlashlight)
        {
            // Assert here because if this isn't true there's something seriously wrong
            Assert(info.FlashlightTexture >= 0 && info.FlashlightTextureFrame >= 0);
            this->BindTexture(SHADER_SAMPLER5, info.FlashlightTexture, info.FlashlightTextureFrame);
            VMatrix worldToTexture;
            ITexture *pFlashlightDepthTexture;
            FlashlightState_t state = pShaderAPI->GetFlashlightStateEx(worldToTexture, &pFlashlightDepthTexture);
            bFlashlightShadows = state.m_bEnableShadows && (pFlashlightDepthTexture != NULL);

            SetFlashLightColorFromState(state, pShaderAPI, PSREG_FLASHLIGHT_COLOR);

            if (pFlashlightDepthTexture && g_pConfig->ShadowDepthTexture() && state.m_bEnableShadows)
            {
                this->BindTexture(SHADER_SAMPLER6, pFlashlightDepthTexture, 0);
                pShaderAPI->BindStandardTexture(SHADER_SAMPLER7, TEXTURE_SHADOW_NOISE_2D);
            }
        }

        // Binding lightmap texture
        if (bLightmapped)
        {
            pShaderAPI->BindStandardTexture(SHADER_SAMPLER8, TEXTURE_LIGHTMAP_BUMPED);
        }

        MaterialFogMode_t fogType = pShaderAPI->GetSceneFogMode();
        int fogIndex = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z) ? 1 : 0;
        int numBones = pShaderAPI->GetCurrentNumBones();

        bool bLightingOnly = mat_fullbright.GetInt() == 2 && !IS_FLAG_SET(MATERIAL_VAR_NO_DEBUG_OVERRIDE);
        bool bLightingPreview = pShaderAPI->GetIntRenderingParameter(INT_RENDERPARM_ENABLE_FIXED_LIGHTING) != 0;

        bool bWriteDepthToAlpha = false;
        bool bWriteWaterFogToAlpha = false;
        if (bFullyOpaque)
        {
            bWriteDepthToAlpha = pShaderAPI->ShouldWriteDepthToDestAlpha();
            bWriteWaterFogToAlpha = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z);
            AssertMsg(!(bWriteDepthToAlpha && bWriteWaterFogToAlpha),
                      "Can't write two values to alpha at the same time.");
        }

        DECLARE_DYNAMIC_VERTEX_SHADER(pbr_vs20);
        SET_DYNAMIC_VERTEX_SHADER_COMBO(SKINNING, numBones > 0);
        SET_DYNAMIC_VERTEX_SHADER_COMBO(DOWATERFOG, fogIndex);
        SET_DYNAMIC_VERTEX_SHADER_COMBO(LIGHTING_PREVIEW, bLightingPreview);
        SET_DYNAMIC_VERTEX_SHADER_COMBO(COMPRESSED_VERTS, (int)vertexCompression);
        SET_DYNAMIC_VERTEX_SHADER_COMBO(NUM_LIGHTS, lightState.m_nNumLights);
        SET_DYNAMIC_VERTEX_SHADER(pbr_vs20);

        DECLARE_DYNAMIC_PIXEL_SHADER(pbr_ps30);
        SET_DYNAMIC_PIXEL_SHADER_COMBO(NUM_LIGHTS, lightState.m_nNumLights);
        SET_DYNAMIC_PIXEL_SHADER_COMBO(WRITEWATERFOGTODESTALPHA, bWriteWaterFogToAlpha);
        SET_DYNAMIC_PIXEL_SHADER_COMBO(WRITE_DEPTH_TO_DESTALPHA, bWriteDepthToAlpha);
        SET_DYNAMIC_PIXEL_SHADER_COMBO(PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo());
        SET_DYNAMIC_PIXEL_SHADER_COMBO(FLASHLIGHTSHADOWS, bFlashlightShadows);
        SET_DYNAMIC_PIXEL_SHADER(pbr_ps30);

        this->SetVertexShaderTextureTransform(VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, info.BaseTextureTransform);
        this->SetModulationPixelShaderDynamicState_LinearColorSpace(1);
        // this->SetAmbientCubeDynamicStateVertexShader(); // not using this

        pShaderAPI->SetPixelShaderStateAmbientLightCube(PSREG_AMBIENT_CUBE, !lightState.m_bAmbientLight);
        pShaderAPI->CommitPixelShaderLighting(PSREG_LIGHT_INFO_ARRAY);
        pShaderAPI->SetPixelShaderFogParams(PSREG_FOG_PARAMS);

        // handle mat_fullbright 2 (diffuse lighting only)
        if (bLightingOnly)
        {
            pShaderAPI->BindStandardTexture(SHADER_SAMPLER0, TEXTURE_GREY); // Albedo
        }

        float vEyePos_SpecExponent[4];
        pShaderAPI->GetWorldSpaceCameraPosition(vEyePos_SpecExponent);
        vEyePos_SpecExponent[3] = 0.0f;
        pShaderAPI->SetPixelShaderConstant(PSREG_EYEPOS_SPEC_EXPONENT, vEyePos_SpecExponent, 1);

        if (bHasFlashlight)
        {
            VMatrix worldToTexture;
            float atten[4], pos[4], tweaks[4];

            const FlashlightState_t &flashlightState = pShaderAPI->GetFlashlightState(worldToTexture);
            SetFlashLightColorFromState(flashlightState, pShaderAPI, PSREG_FLASHLIGHT_COLOR);

            this->BindTexture(SHADER_SAMPLER5, flashlightState.m_pSpotlightTexture,
                              flashlightState.m_nSpotlightTextureFrame);

            atten[0] = flashlightState.m_fConstantAtten; // Set the flashlight attenuation factors
            atten[1] = flashlightState.m_fLinearAtten;
            atten[2] = flashlightState.m_fQuadraticAtten;
            atten[3] = flashlightState.m_FarZ;
            pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_ATTENUATION, atten, 1);

            pos[0] = flashlightState.m_vecLightOrigin[0]; // Set the flashlight origin
            pos[1] = flashlightState.m_vecLightOrigin[1];
            pos[2] = flashlightState.m_vecLightOrigin[2];
            pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_POSITION_RIM_BOOST, pos, 1);

            pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_TO_WORLD_TEXTURE, worldToTexture.Base(), 4);

            // Tweaks associated with a given flashlight
            tweaks[0] = ShadowFilterFromState(flashlightState);
            tweaks[1] = ShadowAttenFromState(flashlightState);
            this->HashShadow2DJitter(flashlightState.m_flShadowJitterSeed, &tweaks[2], &tweaks[3]);
            pShaderAPI->SetPixelShaderConstant(PSREG_ENVMAP_TINT__SHADOW_TWEAKS, tweaks, 1);

            // Dimensions of screen, used for screen-space noise map sampling
            float vScreenScale[4] = {1280.0f / 32.0f, 720.0f / 32.0f, 0, 0};
            int nWidth, nHeight;
            pShaderAPI->GetBackBufferDimensions(nWidth, nHeight);
            vScreenScale[0] = (float)nWidth / 32.0f;
            vScreenScale[1] = (float)nHeight / 32.0f;
            pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_SCREEN_SCALE, vScreenScale, 1);
        }
    }

    this->Draw();
}

END_SHADER
