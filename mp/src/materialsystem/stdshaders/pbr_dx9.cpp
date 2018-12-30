//===================== Copyright (c) Valve Corporation. All Rights Reserved. ======================
//
// Example shader that can be applied to models
//
//==================================================================================================

#include "BaseVSShader.h"
#include "convar.h"
#include "commandbuilder.h"
#include "cpp_shader_constant_register_map.h"

#include "pbr_vs20.inc"
#include "pbr_ps30.inc"

struct PBR_Vars_t
{
	PBR_Vars_t() { memset(this, 0xFF, sizeof(*this)); }

	int baseTexture;
	int baseColor;
	int normalTexture;
	int envMap;
	int baseTextureFrame;
	int baseTextureTransform;
	int alphaTestReference;
	int flashlightTexture;
	int flashlightTextureFrame;
	int pbrLookupTexture;
	int mraoTexture;
	//int metalness;
	//int roughness;
};

static ConVar mat_fullbright("mat_fullbright", "0", FCVAR_CHEAT);
static ConVar mat_specular("mat_specular", "1", FCVAR_CHEAT);

//DEFINE_FALLBACK_SHADER( UnlitGeneric, PBR )
BEGIN_VS_SHADER( PBR, "PBR shader" )

BEGIN_SHADER_PARAMS
	SHADER_PARAM(ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.0", "")
	//SHADER_PARAM(METALNESS, SHADER_PARAM_TYPE_FLOAT, "1.0", "Sets the metalness of the material.")
	//SHADER_PARAM(ROUGHNESS, SHADER_PARAM_TYPE_FLOAT, "1.0", "Sets the roughness of the material.")
	SHADER_PARAM(ENVMAP, SHADER_PARAM_TYPE_ENVMAP, "", "Set the cubemap for this material.")
	SHADER_PARAM(MRAOTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Texture with metalness in R, roughness in G, ambient occlusion in B.")
	SHADER_PARAM_FLAGS(PBRLOOKUP, SHADER_PARAM_TYPE_TEXTURE, "dev/pbr_lookup", "The PBR lookup texture, don't change this.", SHADER_PARAM_NOT_EDITABLE)
	SHADER_PARAM(NORMALTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Normal texture");
END_SHADER_PARAMS

	void SetupVars( PBR_Vars_t& info )
	{
		info.baseTexture = BASETEXTURE;
		info.baseColor = COLOR;
		info.normalTexture = NORMALTEXTURE;
		info.baseTextureFrame = FRAME;
		info.baseTextureTransform = BASETEXTURETRANSFORM;
		info.alphaTestReference = ALPHATESTREFERENCE;
		//info.metalness = METALNESS;
		//info.roughness = ROUGHNESS;
		info.flashlightTexture = FLASHLIGHTTEXTURE;
		info.flashlightTextureFrame = FLASHLIGHTTEXTUREFRAME;
		info.envMap = ENVMAP;
		info.pbrLookupTexture = PBRLOOKUP;
		info.mraoTexture = MRAOTEXTURE;
	}

	SHADER_INIT_PARAMS()
	{
		if (g_pHardwareConfig->SupportsBorderColor())
		{
			params[FLASHLIGHTTEXTURE]->SetStringValue("effects/flashlight_border");
		}
		else
		{
			params[FLASHLIGHTTEXTURE]->SetStringValue("effects/flashlight001");
		}

		// This shader can be used with hw skinning
		SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_HW_SKINNING);

		if (IS_FLAG_SET(MATERIAL_VAR_MODEL)) {
			SET_FLAGS2(MATERIAL_VAR2_LIGHTING_VERTEX_LIT);
		} else {
			SET_FLAGS2(MATERIAL_VAR2_LIGHTING_LIGHTMAP);
			SET_FLAGS2(MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP);
		}
		SET_FLAGS2(MATERIAL_VAR2_USES_ENV_CUBEMAP);
		SET_FLAGS2(MATERIAL_VAR2_USE_FLASHLIGHT);
		SET_FLAGS2(MATERIAL_VAR2_NEEDS_BAKED_LIGHTING_SNAPSHOTS);
		params[PBRLOOKUP]->SetStringValue("dev/pbr_lookup");
		params[FLASHLIGHTTEXTURE]->SetStringValue("effects/flashlight001");

	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		PBR_Vars_t info;
		SetupVars( info );

		Assert(info.flashlightTexture >= 0);
		LoadTexture(info.flashlightTexture, TEXTUREFLAGS_SRGB);
		Assert(info.normalTexture >= 0);
		LoadTexture(info.normalTexture, TEXTUREFLAGS_NORMAL);
		Assert(info.envMap >= 0);
		LoadCubeMap(info.envMap, 0);
		Assert(info.pbrLookupTexture >= 0);
		LoadTexture(info.pbrLookupTexture, TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD);
		Assert(info.mraoTexture >= 0);
		LoadTexture(info.mraoTexture, 0);

		bool bIsBaseTextureTranslucent = false;
		if (params[info.baseTexture]->IsDefined())
		{
			LoadTexture(info.baseTexture, TEXTUREFLAGS_SRGB);

			if (params[info.baseTexture]->GetTextureValue()->IsTranslucent())
			{
				bIsBaseTextureTranslucent = true;
			}
		}
	}

	SHADER_DRAW
	{
		PBR_Vars_t info;
		SetupVars( info );
		bool bHasBaseTexture = (info.baseTexture != -1) && params[info.baseTexture]->IsTexture();
		bool bHasNormalTexture = (info.normalTexture != -1) && params[info.normalTexture]->IsTexture();
		bool bHasMraoTexture = (info.mraoTexture != -1) && params[info.mraoTexture]->IsTexture();
		bool bHasEnvTexture = (info.envMap != -1) && params[info.envMap]->IsTexture();
		bool bIsAlphaTested = IS_FLAG_SET(MATERIAL_VAR_ALPHATEST) != 0;
		bool bHasFlashlight = UsingFlashlight(params);
		bool bHasColor = (info.baseColor != -1) && params[info.baseColor]->IsDefined();

		BlendType_t nBlendType = EvaluateBlendRequirements(info.baseTexture, true);
		bool bFullyOpaque = (nBlendType != BT_BLENDADD) && (nBlendType != BT_BLEND) && !bIsAlphaTested;

		if (IsSnapshotting())
		{
			pShaderShadow->EnableAlphaTest(bIsAlphaTested);

			if (info.alphaTestReference != -1 && params[info.alphaTestReference]->GetFloatValue() > 0.0f)
			{
				pShaderShadow->AlphaFunc(SHADER_ALPHAFUNC_GEQUAL, params[info.alphaTestReference]->GetFloatValue());
			}

			int nShadowFilterMode = 0;
			SetDefaultBlendingShadowState(info.baseTexture, true);

			if (bHasFlashlight)
			{
				nShadowFilterMode = g_pHardwareConfig->GetShadowFilterMode();	// Based upon vendor and device dependent formats
			}

			// Always enable...will bind white if nothing specified...
			pShaderShadow->EnableTexture(SHADER_SAMPLER0, true);		// Base (albedo) map
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER0, false);

			if (bHasFlashlight)
			{
				pShaderShadow->EnableTexture(SHADER_SAMPLER4, true);	// Shadow depth map
				pShaderShadow->SetShadowDepthFiltering(SHADER_SAMPLER4);
				pShaderShadow->EnableSRGBRead(SHADER_SAMPLER4, false);
				pShaderShadow->EnableTexture(SHADER_SAMPLER5, true);	// Noise map
				pShaderShadow->EnableTexture(SHADER_SAMPLER6, true);	// Flashlight cookie
				pShaderShadow->EnableSRGBRead(SHADER_SAMPLER6, true);
				//userDataSize = 4; // tangent S
			}
			if (bHasEnvTexture)
			{
				pShaderShadow->EnableTexture(SHADER_SAMPLER2, true); //Envmap
			}
			pShaderShadow->EnableTexture(SHADER_SAMPLER9, true); // PBR lookup texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER9, false);
			pShaderShadow->EnableTexture(SHADER_SAMPLER7, true); // Lightmap texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER7, false);
			pShaderShadow->EnableTexture(SHADER_SAMPLER10, true); // MRAO texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER10, false);
			pShaderShadow->EnableTexture(SHADER_SAMPLER1, true); // NORMAL texture
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER1, false);

			// Always enable, since flat normal will be bound
			pShaderShadow->EnableTexture(SHADER_SAMPLER3, true);		// Normal map

			//pShaderShadow->EnableTexture(SHADER_SAMPLER5, true);		// Normalizing cube map
			//pShaderShadow->EnableSRGBWrite(true);

			// texcoord0 : base texcoord, texcoord2 : decal hw morph delta
			int pTexCoordDim[5] = { 2, 2, 3 };
			int nTexCoordCount = 1;

			pShaderShadow->DrawFlags(SHADER_DRAW_POSITION | SHADER_DRAW_NORMAL | SHADER_DRAW_TEXCOORD0 | SHADER_DRAW_LIGHTMAP_TEXCOORD1);
			unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
			if (IS_FLAG_SET(MATERIAL_VAR_MODEL))
				pShaderShadow->VertexShaderVertexFormat(flags, nTexCoordCount, pTexCoordDim, 4);
			else
				pShaderShadow->VertexShaderVertexFormat(flags, 3, 0, 0);

			DECLARE_STATIC_VERTEX_SHADER(pbr_vs20);
			SET_STATIC_VERTEX_SHADER(pbr_vs20);

			DECLARE_STATIC_PIXEL_SHADER(pbr_ps30);
			SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHT, bHasFlashlight);
			SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHTDEPTHFILTERMODE, nShadowFilterMode);
			SET_STATIC_PIXEL_SHADER_COMBO(LIGHTMAPPED, !IS_FLAG_SET(MATERIAL_VAR_MODEL));
			SET_STATIC_PIXEL_SHADER_COMBO(CONVERT_TO_SRGB, 0);
			SET_STATIC_PIXEL_SHADER(pbr_ps30);


			DefaultFog();

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
				params[info.baseColor]->GetVecValue(color.Base(), 3);
			else
				color = Vector{ 1.f, 1.f, 1.f };
			pShaderAPI->SetPixelShaderConstant(PSREG_SELFILLUMTINT, color.Base());

			if (bHasEnvTexture)
				BindTexture(SHADER_SAMPLER2, info.envMap, 0);
			else
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER2, TEXTURE_GREY);

			BindTexture(SHADER_SAMPLER9, info.pbrLookupTexture, 0);

			if (bHasNormalTexture)
				BindTexture(SHADER_SAMPLER1, info.normalTexture, 0);
			else
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER1, TEXTURE_NORMALMAP_FLAT);

			if (bHasMraoTexture)
				BindTexture(SHADER_SAMPLER10, info.mraoTexture, 0);
			else
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER10, TEXTURE_WHITE);

			LightState_t lightState = { 0, false, false };
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

				//SetFlashLightColorFromState(state, pShaderAPI, PSREG_FLASHLIGHT_COLOR);

				if (pFlashlightDepthTexture && g_pConfig->ShadowDepthTexture() && state.m_bEnableShadows)
				{
					BindTexture(SHADER_SAMPLER4, pFlashlightDepthTexture, 0);
					pShaderAPI->BindStandardTexture(SHADER_SAMPLER5, TEXTURE_SHADOW_NOISE_2D);
				}
			}

			pShaderAPI->GetDX9LightState(&lightState);
			MaterialFogMode_t fogType = pShaderAPI->GetSceneFogMode();
			int fogIndex = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z) ? 1 : 0;
			int numBones = pShaderAPI->GetCurrentNumBones();

			bool bWriteDepthToAlpha = false;
			bool bWriteWaterFogToAlpha = false;
			if (bFullyOpaque)
			{
				bWriteDepthToAlpha = pShaderAPI->ShouldWriteDepthToDestAlpha();
				bWriteWaterFogToAlpha = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z);
				AssertMsg(!(bWriteDepthToAlpha && bWriteWaterFogToAlpha), "Can't write two values to alpha at the same time.");
			}

			float vEyePos_SpecExponent[4];
			pShaderAPI->GetWorldSpaceCameraPosition(vEyePos_SpecExponent);
			vEyePos_SpecExponent[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant(PSREG_EYEPOS_SPEC_EXPONENT, vEyePos_SpecExponent, 1);

			//LoadBumpLightmapCoordinateAxes_PixelShader(PSREG_CONSTANT_27);
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

			pShaderAPI->SetPixelShaderStateAmbientLightCube(PSREG_AMBIENT_CUBE);
			pShaderAPI->CommitPixelShaderLighting(PSREG_LIGHT_INFO_ARRAY);

			// handle mat_fullbright 2 (diffuse lighting only)
			if (bLightingOnly)
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER0, TEXTURE_GREY);
			}
			if (!mat_specular.GetBool())
			{
				pShaderAPI->BindStandardTexture(SHADER_SAMPLER2, TEXTURE_GREY);
			}

			pShaderAPI->SetPixelShaderFogParams(PSREG_FOG_PARAMS);

			if (bHasFlashlight)
			{
				VMatrix worldToTexture;
				float atten[4], pos[4], tweaks[4];

				const FlashlightState_t &flashlightState = pShaderAPI->GetFlashlightState(worldToTexture);
				//SetFlashLightColorFromState(flashlightState, pShaderAPI, PSREG_FLASHLIGHT_COLOR);

				BindTexture(SHADER_SAMPLER6, flashlightState.m_pSpotlightTexture, flashlightState.m_nSpotlightTextureFrame);

				atten[0] = flashlightState.m_fConstantAtten;		// Set the flashlight attenuation factors
				atten[1] = flashlightState.m_fLinearAtten;
				atten[2] = flashlightState.m_fQuadraticAtten;
				atten[3] = flashlightState.m_FarZ;
				pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_ATTENUATION, atten, 1);

				pos[0] = flashlightState.m_vecLightOrigin[0];		// Set the flashlight origin
				pos[1] = flashlightState.m_vecLightOrigin[1];
				pos[2] = flashlightState.m_vecLightOrigin[2];
				//pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_POSITION_RIM_BOOST, pos, 1);
				pShaderAPI->SetPixelShaderConstant(PSREG_SPEC_RIM_PARAMS, pos, 1);

				pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_TO_WORLD_TEXTURE, worldToTexture.Base(), 4);

				// Tweaks associated with a given flashlight
				tweaks[0] = ShadowFilterFromState(flashlightState);
				tweaks[1] = ShadowAttenFromState(flashlightState);
				HashShadow2DJitter(flashlightState.m_flShadowJitterSeed, &tweaks[2], &tweaks[3]);
				pShaderAPI->SetPixelShaderConstant(PSREG_ENVMAP_TINT__SHADOW_TWEAKS, tweaks, 1);
			}


			// Dimensions of screen, used for screen-space noise map sampling
			//float vScreenScale[4] = { 1280.0f / 32.0f, 720.0f / 32.0f, 0, 0 };
			//int nWidth, nHeight;
			//pShaderAPI->GetBackBufferDimensions(nWidth, nHeight);
			//vScreenScale[0] = (float)nWidth / 32.0f;
			//vScreenScale[1] = (float)nHeight / 32.0f;
			//vScreenScale[2] = (info.metalness != -1 && params[info.metalness]  )? params[info.metalness]->GetFloatValue() : 1.0f;
			//vScreenScale[3] = (info.roughness != -1 && params[info.roughness]->IsDefined())? params[info.roughness]->GetFloatValue() : 1.0f;
			//pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_SCREEN_SCALE, vScreenScale, 1);
		}
		Draw();
	}

END_SHADER

