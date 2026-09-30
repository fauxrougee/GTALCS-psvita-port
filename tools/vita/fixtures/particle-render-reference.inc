// Original particle traversal, used only by the native regression test.
void renderReference()
{
	PUSH_RENDERGROUP("CParticle::Render");

	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void *)rwTEXTUREADDRESSWRAP);
	RwRenderStateSet(rwRENDERSTATETEXTUREPERSPECTIVE, (void *)TRUE);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void *)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void *)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void *)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDINVSRCALPHA);

	CSprite::InitSpriteBuffer2D();

	uint32 flags = DRAW_OPAQUE;

	RwRaster *prevFrame = nil;

	for ( int32 i = 0; i < MAX_PARTICLES; i++ )
	{
		tParticleSystemData *psystem = &mod_ParticleSystemManager.m_aParticles[i];
		bool particleBanned = false;
		CParticle *particle = psystem->m_pParticles;

		RwRaster **frames = psystem->m_ppRaster;
		tParticleType type = psystem->m_Type;

		/*
		something on ps2
		if ( type == PARTICLE_ENGINE_SMOKE
			|| type == PARTICLE_ENGINE_SMOKE2
			|| type == PARTICLE_ENGINE_STEAM
			|| type == PARTICLE_CARFLAME_SMOKE
			|| type == PARTICLE_RUBBER_SMOKE
			|| type == PARTICLE_BURNINGRUBBER_SMOKE
			|| type == PARTICLE_EXHAUST_FUMES
			|| type == PARTICLE_CARCOLLISION_DUST )
		{
			particleBanned = true;
		}
		*/
		if ( particle )
		{
			if ( (flags & DRAW_OPAQUE) != (psystem->Flags & DRAW_OPAQUE)
				|| (flags & DRAW_DARK) != (psystem->Flags & DRAW_DARK) )
			{
				CSprite::FlushSpriteBuffer();

				if ( psystem->Flags & DRAW_OPAQUE )
				{
					RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
					RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDINVSRCALPHA);
				}
				else
				{
					if ( psystem->Flags & DRAW_DARK )
						RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
					else
						RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDONE);

					RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDONE);
				}

				flags = psystem->Flags;
			}

			if ( frames != nil )
			{
				RwRaster *curFrame = *frames;
				if ( curFrame != prevFrame )
				{
					CSprite::FlushSpriteBuffer();
					RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void *)curFrame);
					prevFrame = curFrame;
				}
			}
		}

		while ( particle != nil )
		{
			bool canDraw = true;

			if ( particle->m_nAlpha == 0 )
				canDraw = false;

			// TODO: psp vert buffer check

			if ( canDraw && psystem->m_nFinalAnimationFrame != 0 && frames != nil )
			{
				RwRaster *curFrame = frames[particle->m_nCurrentFrame];
				if ( prevFrame != curFrame )
				{
					CSprite::FlushSpriteBuffer();
					RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void *)curFrame);
					prevFrame = curFrame;
				}
			}

			if ( canDraw && psystem->Flags & DRAWTOP2D )
			{
				float screenZ = CalcScreenZ(particle->m_vecPosition.z);

				float stretchTexW;
				float stretchTexH;

				if ( i == PARTICLE_RAINDROP || i == PARTICLE_RAINDROP_SMALL || i == PARTICLE_RAINDROP_2D )
				{
					stretchTexW = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.x * (float)particle->m_nCurrentFrame + 63.0f;
					stretchTexH = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y * (float)particle->m_nCurrentFrame + 63.0f;
				}
				else
				{
					stretchTexW = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.x + 63.0f;
					stretchTexH = CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y + 63.0f;
				}

				if ( canDraw )
				{
					if ( particle->m_nRotation != 0 )
					{
						CSprite::RenderBufferedOneXLUSprite2D_Rotate_Dimension(
								particle->m_vecPosition.x,
								particle->m_vecPosition.y,
								particle->m_fSize * stretchTexW,
								particle->m_fSize * stretchTexH,
								particle->m_Color,
								particle->m_nColorIntensity,
								DEGTORAD((float)particle->m_nRotation),
								particle->m_nAlpha);
					}
					else
					{
						CSprite::RenderBufferedOneXLUSprite2D(
								particle->m_vecPosition.x,
								particle->m_vecPosition.y,
								particle->m_fSize * stretchTexW,
								particle->m_fSize * stretchTexH,
								particle->m_Color,
								particle->m_nColorIntensity,
								particle->m_nAlpha);
					}
				}

				canDraw = false;
			}

			if ( canDraw )
			{
				CVector coors;
				float w;
				float h;

				if ( CSprite::CalcScreenCoors(particle->m_vecPosition, &coors, &w, &h, true) )
				{

					if ( i == PARTICLE_ENGINE_STEAM
						|| i == PARTICLE_ENGINE_SMOKE
						|| i == PARTICLE_ENGINE_SMOKE2
						|| i == PARTICLE_CARFLAME_SMOKE
						|| i == PARTICLE_CARCOLLISION_DUST
						|| i == PARTICLE_EXHAUST_FUMES
						|| i == PARTICLE_RUBBER_SMOKE
						|| i == PARTICLE_BURNINGRUBBER_SMOKE )
					{
						switch ( TheCamera.GetLookDirection() )
						{
							case LOOKING_LEFT:
							case LOOKING_RIGHT:
								w += CGeneral::GetRandomNumberInRange(1.0f, 7.5f) * psystem->m_vecTextureStretch.x;
								h += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y;
								break;

							default:
								w += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.x;
								h += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y;
								break;
						}
					}
					else if ( i == PARTICLE_WATER_HYDRANT )
					{
						float screenZ = CalcScreenZ(particle->m_vecPosition.z);

						int32 timeLeft = (particle->m_nTimeWhenWillBeDestroyed - CTimer::GetTimeInMilliseconds()) / particle->m_nTimeWhenWillBeDestroyed;

						w += (1.0f - (float)timeLeft) * psystem->m_vecTextureStretch.x;
						h += (1.0f - (float)timeLeft) * psystem->m_vecTextureStretch.y;
					}
					else if ( i == PARTICLE_FLYERS )
					{
						w += psystem->m_vecTextureStretch.x;
						h += psystem->m_vecTextureStretch.y;

						w = Max(w, 12.0f);
						h = Max(h, 12.0f);
					}
					else
					{
						w += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.x;
						h += CGeneral::GetRandomNumberInRange(0.1f, 1.0f) * psystem->m_vecTextureStretch.y;
					}

					if ( particle->m_nRotation != 0 && i != PARTICLE_BEASTIE )
					{
						CSprite::RenderBufferedOneXLUSprite_Rotate_Dimension(coors.x, coors.y, coors.z,
								particle->m_fSize * w, particle->m_fSize * h,
								particle->m_Color.red,
								particle->m_Color.green,
								particle->m_Color.blue,
								particle->m_nColorIntensity,
								1.0f / coors.z,
								DEGTORAD((float)particle->m_nRotation),
								particle->m_nAlpha);
					}
					else if ( psystem->Flags & SCREEN_TRAIL )
					{
						float fRotation;
						float fTrailLength;

						if ( particle->m_fZGround == 0.0f )
						{
							fTrailLength = 0.0f;
							fRotation = 0.0f;
						}
						else
						{
							CVector2D vecDist
							(
								coors.x - particle->m_fZGround,
								coors.y - particle->m_fExpansionRate
							);

							float fDist = vecDist.Magnitude();

							fTrailLength = fDist;

							float fRot = Asin(vecDist.x / fDist);

							fRotation = fRot;

							if ( vecDist.y < 0.0f )
								fRotation = -1.0f * fRot + DEGTORAD(180.0f);

							float fSpeed = particle->m_vecVelocity.Magnitude();

							float fNewTrailLength = fSpeed * CTimer::GetTimeStep() * w * 2.0f;

							if ( fDist > fNewTrailLength )
								fTrailLength = fNewTrailLength;
						}

						CSprite::RenderBufferedOneXLUSprite_Rotate_Dimension(coors.x, coors.y, coors.z,
								particle->m_fSize * w,
								particle->m_fSize * h + fTrailLength * psystem->m_fTrailLengthMultiplier,
								particle->m_Color.red,
								particle->m_Color.green,
								particle->m_Color.blue,
								particle->m_nColorIntensity,
								1.0f / coors.z,
								fRotation,
								particle->m_nAlpha);

						particle->m_fZGround = coors.x;				// WTF ?
						particle->m_fExpansionRate =  coors.y;		// WTF ?
					}
					else if ( psystem->Flags & SPEED_TRAIL )
					{
						CVector vecPrevPos = particle->m_vecPosition - particle->m_vecVelocity;
						float fRotation;
						float fTrailLength;
						CVector vecScreenPosition;

						if ( CSprite::CalcScreenCoors(vecPrevPos, &vecScreenPosition, &fTrailLength, &fRotation, true) )
						{
							CVector2D vecDist
							(
								coors.x - vecScreenPosition.x,
								coors.y - vecScreenPosition.y
							);

							float fDist = vecDist.Magnitude();

							fTrailLength = fDist;

							float fRot = Asin(vecDist.x / fDist);

							fRotation = fRot;

							if ( vecDist.y < 0.0f )
								fRotation = -1.0f * fRot + DEGTORAD(180.0f);
						}
						else
						{
							fRotation = 0.0f;
							fTrailLength = 0.0f;
						}

						CSprite::RenderBufferedOneXLUSprite_Rotate_Dimension(coors.x, coors.y, coors.z,
								particle->m_fSize * w,
								particle->m_fSize * h + fTrailLength * psystem->m_fTrailLengthMultiplier,
								particle->m_Color.red,
								particle->m_Color.green,
								particle->m_Color.blue,
								particle->m_nColorIntensity,
								1.0f / coors.z,
								fRotation,
								particle->m_nAlpha);
					}
					else if ( psystem->Flags & VERT_TRAIL )
					{
						float fTrailLength = Abs(particle->m_vecVelocity.z * 10.0f);

						CSprite::RenderBufferedOneXLUSprite(coors.x, coors.y, coors.z,
								particle->m_fSize * w,
								(particle->m_fSize + fTrailLength * psystem->m_fTrailLengthMultiplier) * h,
								particle->m_Color.red,
								particle->m_Color.green,
								particle->m_Color.blue,
								particle->m_nColorIntensity,
								1.0f / coors.z,
								particle->m_nAlpha);
					}
					else if ( i == PARTICLE_RAINDROP_SMALL )
					{
						CSprite::RenderBufferedOneXLUSprite(coors.x, coors.y, coors.z,
								particle->m_fSize * w * 0.05f,
								particle->m_fSize * h,
								particle->m_Color.red,
								particle->m_Color.green,
								particle->m_Color.blue,
								particle->m_nColorIntensity,
								1.0f / coors.z,
								particle->m_nAlpha);
					}
					else
					{
						CSprite::RenderBufferedOneXLUSprite(coors.x, coors.y, coors.z,
								particle->m_fSize * w,
								particle->m_fSize * h,
								particle->m_Color.red,
								particle->m_Color.green,
								particle->m_Color.blue,
								particle->m_nColorIntensity,
								1.0f / coors.z,
								particle->m_nAlpha);
					}
				}
			}

			particle = particle->m_pNext;
		}

		CSprite::FlushSpriteBuffer();

	}

	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void *)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void *)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void *)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void *)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void *)rwBLENDINVSRCALPHA);

	POP_RENDERGROUP();
}
