#include "ldraw.h"
#include "lcanvas.h"
#include <TFE_Game/igame.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <assert.h>
#include <map>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	static u8* s_bitmap = nullptr;
	static s32 s_bitmapWidth = 0;
	static s32 s_bitmapHeight = 0;

	void ldraw_init(s16 w, s16 h)
	{
		if (w != s_bitmapWidth || h != s_bitmapHeight)
		{
			game_free(s_bitmap);
			s_bitmap = (u8*)game_alloc(w * h);

			s_bitmapWidth  = w;
			s_bitmapHeight = h;
		}
	}

	void ldraw_destroy()
	{
		game_free(s_bitmap);
		s_bitmap = nullptr;
		s_bitmapWidth = 0;
		s_bitmapHeight = 0;
	}

	u8* ldraw_getBitmap()
	{
		return s_bitmap;
	}

	JBool drawClippedColorRect(LRect* rect, u8 color)
	{
		u8* framebuffer = s_bitmap;
		const u32 stride = s_bitmapWidth;

		LRect clipRect;
		lcanvas_getClip(&clipRect);

		LRect drawRect = *rect;
		if (!lrect_clip(&drawRect, &clipRect))
		{
			return JFALSE;
		}

		for (s32 y = drawRect.top; y < drawRect.bottom; y++)
		{
			u8* output = &framebuffer[y * stride];
			for (s32 x = drawRect.left; x < drawRect.right; x++)
			{
				output[x] = color;
			}
		}
		return JTRUE;
	}

	void drawDeltaIntoBitmap(s16* data, s16 x, s16 y, u8* framebuffer, s32 stride)
	{
		const u8* srcImage = (u8*)data;
		while (1)
		{
			const s16* deltaLine = (s16*)srcImage;
			s16 sizeAndType = deltaLine[0];
			if (sizeAndType == 0)
			{
				break;
			}

			s16 xStart = deltaLine[1] + x;
			s16 yStart = deltaLine[2] + y;
			// Size of the Delta Line structure.
			srcImage += sizeof(s16) * 3;

			const JBool rle = (sizeAndType & 1) ? JTRUE : JFALSE;
			s32 pixelCount = (sizeAndType >> 1) & 0x3fff;
			u8* dstImage = &framebuffer[yStart*stride + xStart];

			while (pixelCount > 0)
			{
				if (rle)
				{
					//read count byte...
					u8 count = *srcImage; srcImage++;
					if (!(count & 1)) // direct
					{
						count >>= 1;
						for (s32 p = 0; p < count; p++, dstImage++, srcImage++)
						{
							*dstImage = *srcImage;
						}
						pixelCount -= count;
					}
					else	//rle
					{
						count >>= 1;
						const u8 pixel = *srcImage; srcImage++;
						for (s32 p = 0; p < count; p++, dstImage++)
						{
							*dstImage = pixel;
						}
						pixelCount -= count;
					}
				}
				else
				{
					for (s32 p = 0; p < pixelCount; p++, dstImage++, srcImage++)
					{
						*dstImage = *srcImage;
					}
					pixelCount = 0;
				}
			}
		}
	}

	void deltaImage(s16* data, s16 x, s16 y)
	{
		drawDeltaIntoBitmap(data, x, y, s_bitmap, s_bitmapWidth);
	}

	void deltaClip(s16* data, s16 x, s16 y)
	{
		u8* framebuffer = s_bitmap;
		const u32 stride = s_bitmapWidth;

		LRect clipRect;
		lcanvas_getClip(&clipRect);

		u8* srcImage = (u8*)data;
		while (1)
		{
			const s16* deltaLine = (s16*)srcImage;
			s16 sizeAndType = deltaLine[0];
			s16 xStart = deltaLine[1] + x;
			s16 yStart = deltaLine[2] + y;
			srcImage += 3 * sizeof(s16);

			if (sizeAndType == 0) { break; }

			const JBool rle = (sizeAndType & 1) ? JTRUE : JFALSE;
			s32 pixelCount = (sizeAndType >> 1) & 0x3fff;
			u8* dstImage = &framebuffer[yStart*stride];
			
			s16 xCur = xStart;
			s16 yCur = yStart;
			JBool writeRow = (yCur >= clipRect.top && yCur < clipRect.bottom) ? JTRUE : JFALSE;

			while (pixelCount > 0)
			{
				if (rle)
				{
					//read count byte...
					u8 count = *srcImage; srcImage++;
					if (!(count & 1)) // direct
					{
						count >>= 1;
						for (s32 p = 0; p < count; p++, srcImage++, xCur++)
						{
							if (writeRow && xCur >= clipRect.left && xCur < clipRect.right)
							{
								dstImage[xCur] = *srcImage;
							}
						}
						pixelCount -= count;
					}
					else	//rle
					{
						count >>= 1;
						const u8 pixel = *srcImage; srcImage++;
						for (s32 p = 0; p < count; p++, xCur++)
						{
							if (writeRow && xCur >= clipRect.left && xCur < clipRect.right)
							{
								dstImage[xCur] = pixel;
							}
						}
						pixelCount -= count;
					}
				}
				else
				{
					for (s32 p = 0; p < pixelCount; p++, srcImage++, xCur++)
					{
						if (writeRow && xCur >= clipRect.left && xCur < clipRect.right)
						{
							dstImage[xCur] = *srcImage;
						}
					}
					pixelCount = 0;
				}
			}
		}
	}

	void deltaFlip(s16* data, s16 x, s16 y, s16 w)
	{
		u8* framebuffer = s_bitmap;
		const u32 stride = s_bitmapWidth;

		const u8* srcImage = (u8*)data;
		while (1)
		{
			const s16* deltaLine = (s16*)srcImage;
			s16 sizeAndType = deltaLine[0];
			if (sizeAndType == 0)
			{
				break;
			}

			s16 xCur = w - deltaLine[1] + x;
			s16 yStart = deltaLine[2] + y;
			// Size of the Delta Line structure.
			srcImage += sizeof(s16) * 3;

			const JBool rle = (sizeAndType & 1) ? JTRUE : JFALSE;
			s32 pixelCount = (sizeAndType >> 1) & 0x3fff;
			u8* dstImage = &framebuffer[yStart*stride];

			while (pixelCount > 0)
			{
				if (rle)
				{
					//read count byte...
					u8 count = *srcImage; srcImage++;
					if (!(count & 1)) // direct
					{
						count >>= 1;
						for (s32 p = 0; p < count; p++, xCur--, srcImage++)
						{
							dstImage[xCur] = *srcImage;
						}
						pixelCount -= count;
					}
					else	//rle
					{
						count >>= 1;
						const u8 pixel = *srcImage; srcImage++;
						for (s32 p = 0; p < count; p++, xCur--)
						{
							dstImage[xCur] = pixel;
						}
						pixelCount -= count;
					}
				}
				else
				{
					for (s32 p = 0; p < pixelCount; p++, xCur--, srcImage++)
					{
						dstImage[xCur] = *srcImage;
					}
					pixelCount = 0;
				}
			}
		}
	}

	void deltaFlipClip(s16* data, s16 x, s16 y, s16 w)
	{
		u8* framebuffer = s_bitmap;
		const u32 stride = s_bitmapWidth;

		LRect clipRect;
		lcanvas_getClip(&clipRect);

		u8* srcImage = (u8*)data;
		while (1)
		{
			const s16* deltaLine = (s16*)srcImage;
			s16 sizeAndType = deltaLine[0];
			s16 xStart = deltaLine[1] + x;
			s16 yStart = deltaLine[2] + y;
			srcImage += 3 * sizeof(s16);

			if (sizeAndType == 0) { break; }

			const JBool rle = (sizeAndType & 1) ? JTRUE : JFALSE;
			s32 pixelCount = (sizeAndType >> 1) & 0x3fff;
			u8* dstImage = &framebuffer[yStart*stride];

			s16 xCur = w - deltaLine[1] + x;
			s16 yCur = yStart;
			JBool writeRow = (yCur >= clipRect.top && yCur < clipRect.bottom) ? JTRUE : JFALSE;

			while (pixelCount > 0)
			{
				if (rle)
				{
					//read count byte...
					u8 count = *srcImage; srcImage++;
					if (!(count & 1)) // direct
					{
						count >>= 1;
						for (s32 p = 0; p < count; p++, srcImage++, xCur--)
						{
							if (writeRow && xCur >= clipRect.left && xCur < clipRect.right)
							{
								dstImage[xCur] = *srcImage;
							}
						}
						pixelCount -= count;
					}
					else	//rle
					{
						count >>= 1;
						const u8 pixel = *srcImage; srcImage++;
						for (s32 p = 0; p < count; p++, xCur--)
						{
							if (writeRow && xCur >= clipRect.left && xCur < clipRect.right)
							{
								dstImage[xCur] = pixel;
							}
						}
						pixelCount -= count;
					}
				}
				else
				{
					for (s32 p = 0; p < pixelCount; p++, srcImage++, xCur--)
					{
						if (writeRow && xCur >= clipRect.left && xCur < clipRect.right)
						{
							dstImage[xCur] = *srcImage;
						}
					}
					pixelCount = 0;
				}
			}
		}
	}
}