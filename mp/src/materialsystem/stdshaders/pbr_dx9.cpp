//===================== Copyright (c) Valve Corporation. All Rights Reserved. ======================
//
// Physically Based Rendering shader for brushes and models
//
//==================================================================================================

#include "BaseVSShader.h"
#include "cpp_shader_constant_register_map.h"

#include "pbr_vs20.inc"
#include "pbr_ps30.inc"

static ConVar mat_fullbright("mat_fullbright", "0", FCVAR_CHEAT);
static ConVar mat_specular("mat_specular", "1", FCVAR_CHEAT);

struct PBR_Vars_t
{
	PBR_Vars_t()
    { 
        memset(this, 0xFF, sizeof(*this));
    }

	int baseTexture;
	int baseColor;
	int normalTexture;
	int bumpMap;
	int envMap;
	int baseTextureFrame;
	int baseTextureTransform;
	int alphaTestReference;
	int flashlightTexture;
	int flashlightTextureFrame;
	int emissionTexture;
	int mraoTexture;
	int useEnvAmbient;
};

// DEFINE_FALLBACK_SHADER( UnlitGeneric, PBR )
BEGIN_VS_SHADER(PBR, "PBR shader")

    BEGIN_SHADER_PARAMS;
	    SHADER_PARAM(ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0", "");
	    SHADER_PARAM(ENVMAP, SHADER_PARAM_TYPE_ENVMAP, "", "Set the cubemap for this material.");
	    SHADER_PARAM(MRAOTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Texture with metalness in R, roughness in G, ambient occlusion in B.");
	    SHADER_PARAM(EMISSIONTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Emission texture");
        SHADER_PARAM(NORMALTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Normal texture (deprecated, use $bumpmap)");
		SHADER_PARAM(BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Normal texture");
        SHADER_PARAM(USEENVAMBIENT, SHADER_PARAM_TYPE_BOOL, "0", "Use the cubemaps to compute ambient light.");
    END_SHADER_PARAMS;

	void SetupVars(PBR_Vars_t &info)
	{
		info.baseTexture = BASETEXTURE;
		info.baseColor = COLOR;
		info.normalTexture = NORMALTEXTURE;
		info.bumpMap = BUMPMAP;
		info.baseTextureFrame = FRAME;
		info.baseTextureTransform = BASETEXTURETRANSFORM;
		info.alphaTestReference = ALPHATESTREFERENCE;
		info.flashlightTexture = FLASHLIGHTTEXTURE;
		info.flashlightTextureFrame = FLASHLIGHTTEXTUREFRAME;
		info.envMap = ENVMAP;
		info.emissionTexture = EMISSIONTEXTURE;
		info.mraoTexture = MRAOTEXTURE;
		info.useEnvAmbient = USEENVAMBIENT;
    };

	SHADER_INIT_PARAMS()
	{
        // Fallback for changed parameter
		if (params[NORMALTEXTURE]->IsDefined())
			params[BUMPMAP]->SetStringValue(params[NORMALTEXTURE]->GetStringValue());

		// Dynamic lights need a bumpmap
		if (!params[BUMPMAP]->IsDefined())
			params[BUMPMAP]->SetStringValue("dev/flat_normal");

		if (g_pHardwareConfig->SupportsBorderColor())
		{
			params[FLASHLIGHTTEXTURE]->SetStringValue("effects/flashlight_border");
		}
		else
		{
			params[FLASHLIGHTTEXTURE]->SetStringValue("effects/flashlight001");
		}
    };

	SHADER_FALLBACK
	{
		return 0;
    };

	SHADER_INIT
	{
		PBR_Vars_t info;
		SetupVars(info);

		Assert(info.flashlightTexture >= 0);
		LoadTexture(info.flashlightTexture, TEXTUREFLAGS_SRGB);
		
		Assert(info.bumpMap >= 0);
		LoadBumpMap(info.bumpMap);
		
		Assert(info.envMap >= 0);
		int envMapFlags = g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0;
		envMapFlags |= TEXTUREFLAGS_ALL_MIPS;
		LoadCubeMap(info.envMap, envMapFlags);
		
		if (info.emissionTexture >= 0 && params[EMISSIONTEXTURE]->IsDefined())
			LoadTexture(info.emissionTexture, TEXTUREFLAGS_SRGB);

		Assert(info.mraoTexture >= 0);
		LoadTexture(info.mraoTexture, 0);

		//bool bIsBaseTextureTranslucent = false;
		if (params[info.baseTexture]->IsDefined())
		{
			LoadTexture(info.baseTexture, TEXTUREFLAGS_SRGB);

			//if (params[info.baseTexture]->GetTextureValue()->IsTranslucent())
			//{
			//	bIsBaseTextureTranslucent = true;
			//}
		}

        if (IS_FLAG_SET(MATERIAL_VAR_MODEL))
        {
            SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_HW_SKINNING);             // Required for skinning
            SET_FLAGS2(MATERIAL_VAR2_LIGHTING_VERTEX_LIT);              // Required for dynamic lighting
            SET_FLAGS2(MATERIAL_VAR2_NEEDS_BAKED_LIGHTING_SNAPSHOTS);   // Required for ambient cube
            SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_FLASHLIGHT);              // Required for flashlight
            SET_FLAGS2(MATERIAL_VAR2_USE_FLASHLIGHT);                   // Required for flashlight
        }
        else
        {
			SET_FLAGS2(MATERIAL_VAR2_LIGHTING_LIGHTMAP);			    // Required for lightmaps
            SET_FLAGS2(MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP);         // Required for lightmaps
            SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_FLASHLIGHT);              // Required for flashlight
            SET_FLAGS2(MATERIAL_VAR2_USE_FLASHLIGHT);                   // Required for flashlight
        }
    };

	SHADER_DRAW
	{
		PBR_Vars_t info;
		SetupVars(info);

		bool bHasBaseTexture = (info.baseTexture != -1) && params[info.baseTexture]->IsTexture();
		bool bHasNormalTexture = (info.normalTexture != -1) && params[info.normalTexture]->IsTexture();
		bool bHasMraoTexture = (info.mraoTexture != -1) && params[info.mraoTexture]->IsTexture();
		bool bHasEmissionTexture = (info.emissionTexture != -1) && params[info.emissionTexture]->IsTexture();
		bool bHasEnvTexture = (info.envMap != -1) && params[info.envMap]->IsTexture();
		bool bIsAlphaTested = IS_FLAG_SET(MATERIAL_VAR_ALPHATEST) != 0;
		bool bHasFlashlight = UsingFlashlight(params);
		bool bHasColor = (info.baseColor != -1) && params[info.baseColor]->IsDefined();
		bool bLightMapped = !IS_FLAG_SET(MATERIAL_VAR_MODEL);
		bool bUseEnvAmbient = (info.useEnvAmbient != -1) && (params[info.useEnvAmbient]->GetIntValue() == 1);


		int iEnvMapLOD = 6;
		auto envTexture = params[info.envMap]->GetTextureValue();
		if (envTexture)
		{
			// Get power of 2 of texture width;
			int width = envTexture->GetMappingWidth();
			int mips = 0;
			while (width >>= 1)
				++mips;

			iEnvMapLOD = mips; //Cubemap has 4 sides so 2 mips less.
		}
		if (iEnvMapLOD > 12)
			iEnvMapLOD = 12;
		if (iEnvMapLOD < 4)
			iEnvMapLOD = 4;

		BlendType_t nBlendType = EvaluateBlendRequirements(info.baseTexture, true);
		bool bFullyOpaque = (nBlendType != BT_BLENDADD) && (nBlendType != BT_BLEND) && !bIsAlphaTested;

		if (IsSnapshotting())
		{

			pShaderShadow->EnableAlphaTest(bIsAlphaTested);

			if (info.alphaTestReference != -1 && params[info.alphaTestReference]->GetFloatValue() > 0.0f)
			{
				pShaderShadow->AlphaFunc(SHADER_ALPHAFUNC_GEQUAL, params[info.alphaTestReference]->GetFloatValue());
			}

			SetDefaultBlendingShadowState(info.baseTexture, true);

			int nShadowFilterMode = bHasFlashlight ? g_pHardwareConfig->GetShadowFilterMode() : 0;

			pShaderShadow->EnableTexture(SHADER_SAMPLER0, true);  // Basecolor texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER0, true); // Basecolor is sRGB
			pShaderShadow->EnableTexture(SHADER_SAMPLER11, true); // Emission texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER11, true); // Emission is sRGB
			pShaderShadow->EnableTexture(SHADER_SAMPLER7, true); // Lightmap texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER7, false); // Lightmaps aren't sRGB
			pShaderShadow->EnableTexture(SHADER_SAMPLER10, true); // MRAO texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER10, false); // MRAO isn't sRGB
			pShaderShadow->EnableTexture(SHADER_SAMPLER1, true); // Normal texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER1, false); // Normals aren't sRGB

			if (bHasFlashlight)
			{
				pShaderShadow->EnableTexture(SHADER_SAMPLER4, true); // Shadow depth map
				pShaderShadow->SetShadowDepthFiltering(SHADER_SAMPLER4);
				pShaderShadow->EnableSRGBRead(SHADER_SAMPLER4, false);
				pShaderShadow->EnableTexture(SHADER_SAMPLER5, true); // Noise map
				pShaderShadow->EnableTexture(SHADER_SAMPLER6, true); // Flashlight cookie
				pShaderShadow->EnableSRGBRead(SHADER_SAMPLER6, true);
			}

			if (bHasEnvTexture)
			{
				pShaderShadow->EnableTexture(SHADER_SAMPLER2, true); // Envmap
				if (g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE)
				{
					pShaderShadow->EnableSRGBRead(SHADER_SAMPLER2, true); // Envmap is only sRGB with HDR disabled?
				}
			}

			// Enabling sRGB writing.
			// See common_ps_fxc.h line 349.
			// PS2b shaders and up write sRGB.
			pShaderShadow->EnableSRGBWrite(true);

			if (IS_FLAG_SET(MATERIAL_VAR_MODEL))
			{
				// We only need the position and surface normal.
                unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
				// We need three texcoords, all in the default float2 size.
				pShaderShadow->VertexShaderVertexFormat(flags, 1, 0, 0);
			}
			else
			{
				// We need the position, surface normal, and vertex compression format.
                unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL;
				// We only need one texcoord, in the default float2 size.
				pShaderShadow->VertexShaderVertexFormat(flags, 3, 0, 0);
			}

			DECLARE_STATIC_VERTEX_SHADER(pbr_vs20);
			SET_STATIC_VERTEX_SHADER(pbr_vs20);

			DECLARE_STATIC_PIXEL_SHADER(pbr_ps30);
			SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHT, bHasFlashlight);
			SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHTDEPTHFILTERMODE, nShadowFilterMode);
			SET_STATIC_PIXEL_SHADER_COMBO(LIGHTMAPPED, bLightMapped);
			SET_STATIC_PIXEL_SHADER_COMBO(USEENVAMBIENT, bUseEnvAmbient);
			SET_STATIC_PIXEL_SHADER_COMBO(EMISSIVE, bHasEmissionTexture);
			SET_STATIC_PIXEL_SHADER(pbr_ps30);

			DefaultFog(); // I think this is correct

			// HACK HACK HACK - enable alpha writes all the time so that we have them for underwater stuff
			pShaderShadow->EnableAlphaWrites(bFullyOpaque);
		}
		else // not snapshotting -- begin dynamic state
		{
			bool bLightingOnly = mat_fullbright.GetInt() == 2 && !IS_FLAG_SET(MATERIAL_VAR_NO_DEBUG_OVERRIDE);

			if (bHasBaseTexture)
			{
				BindTexture(SHADER_SAMPLER0, info.baseTexture, info.baseTextureFrame);
			}
			else
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER0, TEXTURE_WHITE);
			}

			Vector color;
			if (bHasColor)
			{
				params[info.baseColor]->GetVecValue(color.Base(), 3);
			}
			else
			{
				color = Vector{1.f, 1.f, 1.f};
			}
			pShaderAPI->SetPixelShaderConstant(PSREG_SELFILLUMTINT, color.Base());

			if (bHasEnvTexture)
			{
				BindTexture(SHADER_SAMPLER2, info.envMap, 0);
			}
			else
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER2, TEXTURE_BLACK);
			}

			if (bHasEmissionTexture)
			{
				BindTexture(SHADER_SAMPLER11, info.emissionTexture, 0);
			}
			else
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER11, TEXTURE_BLACK);
			}

			if (bHasNormalTexture)
			{
				BindTexture(SHADER_SAMPLER1, info.normalTexture, 0);
			}
			else
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER1, TEXTURE_NORMALMAP_FLAT);
			}

			if (bHasMraoTexture)
			{
				BindTexture(SHADER_SAMPLER10, info.mraoTexture, 0);
			}
			else
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER10, TEXTURE_WHITE);
			}

			LightState_t lightState;
			pShaderAPI->GetDX9LightState(&lightState);
			if (!IS_FLAG_SET(MATERIAL_VAR_MODEL))
			{
				lightState.m_bAmbientLight = false;
				lightState.m_nNumLights = 0;
			}

			bool bFlashlightShadows = false;
			if (bHasFlashlight)
			{
				Assert(info.flashlightTexture >= 0 && info.flashlightTextureFrame >= 0);
				Assert(params[info.flashlightTexture]->IsTexture());
				BindTexture(SHADER_SAMPLER6, info.flashlightTexture, info.flashlightTextureFrame);
				VMatrix worldToTexture;
				ITexture *pFlashlightDepthTexture;
				FlashlightState_t state = pShaderAPI->GetFlashlightStateEx(worldToTexture, &pFlashlightDepthTexture);
				bFlashlightShadows = state.m_bEnableShadows && (pFlashlightDepthTexture != NULL);

				SetFlashLightColorFromState(state, pShaderAPI, PSREG_FLASHLIGHT_COLOR);

				if (pFlashlightDepthTexture && g_pConfig->ShadowDepthTexture() && state.m_bEnableShadows)
				{
					BindTexture(SHADER_SAMPLER4, pFlashlightDepthTexture, 0);
					pShaderAPI->BindStandardTexture(SHADER_SAMPLER5, TEXTURE_SHADOW_NOISE_2D);
				}
			}

			MaterialFogMode_t fogType = pShaderAPI->GetSceneFogMode();
			int fogIndex = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z) ? 1 : 0;
			int numBones = pShaderAPI->GetCurrentNumBones();

			bool bWriteDepthToAlpha = false;
			bool bWriteWaterFogToAlpha = false;
			if (bFullyOpaque)
			{
				bWriteDepthToAlpha = pShaderAPI->ShouldWriteDepthToDestAlpha();
				bWriteWaterFogToAlpha = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z);
				AssertMsg(!(bWriteDepthToAlpha && bWriteWaterFogToAlpha),
						"Can't write two values to alpha at the same time.");
			}

			float vEyePos_SpecExponent[4];
			pShaderAPI->GetWorldSpaceCameraPosition(vEyePos_SpecExponent);
			
			// This has some spare space
			vEyePos_SpecExponent[3] = iEnvMapLOD;
			pShaderAPI->SetPixelShaderConstant(PSREG_EYEPOS_SPEC_EXPONENT, vEyePos_SpecExponent, 1);

			s_pShaderAPI->BindStandardTexture(SHADER_SAMPLER7, TEXTURE_LIGHTMAP_BUMPED);

			DECLARE_DYNAMIC_VERTEX_SHADER(pbr_vs20);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(DOWATERFOG, fogIndex);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(SKINNING, numBones > 0);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(LIGHTING_PREVIEW, pShaderAPI->GetIntRenderingParameter(INT_RENDERPARM_ENABLE_FIXED_LIGHTING) != 0);
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

			SetVertexShaderTextureTransform(VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, info.baseTextureTransform);
			SetModulationPixelShaderDynamicState_LinearColorSpace(1);

            // Send ambient cube to the pixel shader, force to black if not available
            pShaderAPI->SetPixelShaderStateAmbientLightCube(PSREG_AMBIENT_CUBE, !lightState.m_bAmbientLight);

            // Send lighting array to the pixel shader
            pShaderAPI->CommitPixelShaderLighting(PSREG_LIGHT_INFO_ARRAY);

			// handle mat_fullbright 2 (diffuse lighting only)
			if (bLightingOnly)
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER0, TEXTURE_GREY); // basecolor
			}

			// handle mat_specular 0 (no envmap reflections)
			if (!mat_specular.GetBool())
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER2, TEXTURE_BLACK); // envmap
			}

			pShaderAPI->SetPixelShaderFogParams(PSREG_FOG_PARAMS);

			// set up shader modulation color
			float modulationColor[4] = { 1.0, 1.0, 1.0, 1.0 };
			ComputeModulationColor(modulationColor);
			float flLScale = pShaderAPI->GetLightMapScaleFactor();
			modulationColor[0] *= flLScale;
			modulationColor[1] *= flLScale;
			modulationColor[2] *= flLScale;
			pShaderAPI->SetPixelShaderConstant(PSREG_DIFFUSE_MODULATION, modulationColor);

			if (bHasFlashlight)
			{
				VMatrix worldToTexture;
				float atten[4], pos[4], tweaks[4];

				const FlashlightState_t &flashlightState = pShaderAPI->GetFlashlightState(worldToTexture);
				SetFlashLightColorFromState(flashlightState, pShaderAPI, PSREG_FLASHLIGHT_COLOR);

				BindTexture(SHADER_SAMPLER6, flashlightState.m_pSpotlightTexture, flashlightState.m_nSpotlightTextureFrame);

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
				HashShadow2DJitter(flashlightState.m_flShadowJitterSeed, &tweaks[2], &tweaks[3]);
				pShaderAPI->SetPixelShaderConstant(PSREG_ENVMAP_TINT__SHADOW_TWEAKS, tweaks, 1);
			}

		}

		Draw();
    };

END_SHADER;
