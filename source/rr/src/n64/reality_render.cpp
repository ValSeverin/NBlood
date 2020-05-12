// Copyright 2020 Nuke.YKT, EDuke32 developers
// Polymost code: Copyright Ken Silverman, Copyright (c) 2018, Alex Dawson
#include "build.h"
#include "colmatch.h"
#include "reality.h"
#include "../duke3d.h"

tileinfo_t rt_tileinfo[RT_TILENUM];
int32_t rt_tilemap[MAXTILES];
intptr_t rt_waloff[RT_TILENUM];
char rt_walock[RT_TILENUM];

float rt_viewhorizang;

struct maskdraw_t {
    int dist;
    uint16_t index;
    int16_t sectnum;
};

maskdraw_t maskdrawlist[10240];
static int sortspritescnt = 0;

static int globalposx, globalposy, globalposz, rt_smoothRatio;
static fix16_t globalang;

static int globalpal, globalshade;

extern void (*gloadtile_n64)(int32_t dapic, int32_t dapal, int32_t tintpalnum, int32_t dashade, int32_t dameth, pthtyp* pth, int32_t doalloc);

static bool RT_TileLoad(int16_t tilenum);
static void rt_gloadtile_n64(int32_t dapic, int32_t dapal, int32_t tintpalnum, int32_t dashade, int32_t dameth, pthtyp* pth, int32_t doalloc);

void RT_LoadTiles(void)
{
    const int tileinfoOffset = 0x90bf0;
    Blseek(rt_group, tileinfoOffset, SEEK_SET);
    if (Bread(rt_group, rt_tileinfo, sizeof(rt_tileinfo)) != sizeof(rt_tileinfo))
    {
        initprintf("RT_LoadTiles: file read error");
        return;
    }

    Bmemset(rt_tilemap, -1, sizeof(rt_tilemap));
    Bmemset(rt_waloff, 0, sizeof(rt_waloff));
    Bmemset(rt_walock, 0, sizeof(rt_walock));

    for (int i = 0; i < RT_TILENUM; i++)
    {
        auto &t = rt_tileinfo[i];
        t.fileoff = B_BIG32(t.fileoff);
        t.waloff = B_BIG32(t.waloff);
        t.picanm = B_BIG32(t.picanm);
        t.sizx = B_BIG16(t.sizx);
        t.sizy = B_BIG16(t.sizy);
        t.filesiz = B_BIG16(t.filesiz);
        t.dimx = B_BIG16(t.dimx);
        t.dimy = B_BIG16(t.dimy);
        t.flags = B_BIG16(t.flags);
        t.tile = B_BIG16(t.tile);

        rt_tilemap[t.tile] = i;
        tilesiz[t.tile].x = t.sizx;
        tilesiz[t.tile].y = t.sizy;
        tileConvertAnimFormat(t.tile, t.picanm);
        tileUpdatePicSiz(t.tile);
    }

    rt_tileload_callback = RT_TileLoad;
    gloadtile_n64 = rt_gloadtile_n64;
#if 0
    for (auto& t : rt_tileinfo)
    {
        char *data = (char*)tileCreate(t.tile, t.sizx, t.sizy);
        int bufsize = 0;
        if (t.flags & RT_TILE8BIT)
        {
            bufsize = t.dimx * t.dimy;
        }
        else
        {
            bufsize = (t.dimx * t.dimy) / 2 + 32;
        }
        tileConvertAnimFormat(t.tile, t.picanm);
        char *inbuf = (char*)Xmalloc(t.filesiz);
        char *outbuf = (char*)Xmalloc(bufsize);
        Blseek(rt_group, dataOffset+t.fileoff, SEEK_SET);
        Bread(rt_group, inbuf, t.filesiz);
        if (RNCDecompress(inbuf, outbuf) == -1)
        {
            Bmemcpy(outbuf, inbuf, bufsize);
        }
        Xfree(inbuf);
        if (t.flags & RT_TILE8BIT)
        {
            for (int i = 0; i < t.sizx; i++)
            {
                for (int j = 0; j < t.sizy; j++)
                {
                    int ii = t.dimx - 1 - ((t.sizx - i - 1) * t.dimx) / t.sizx;
                    int jj = t.dimy - 1 - ((t.sizy - j - 1) * t.dimy) / t.sizy;
                    data[i*t.sizy+j] = outbuf[j*t.dimx+i];
                }
            }
        }
        else
        {
            int palremap[16];
            char *pix = outbuf+32;
            for (int i = 0; i < 16; i++)
            {
                int t = (outbuf[i*2+1] << 8) + outbuf[i*2];
                int r = (t >> 11) & 31;
                int g = (t >> 6) & 31;
                int b = (t >> 1) & 31;
                int a = (t >> 0) & 1;
                r = (r << 3) + (r >> 2);
                g = (g << 3) + (g >> 2);
                b = (b << 3) + (b >> 2);
                if (a == 0)
                    palremap[i] = 255;
                else
                {
                    palremap[i] = paletteGetClosestColor(r, g, b);
                }
            }
            for (int i = 0; i < t.sizx; i++)
            {
                for (int j = 0; j < t.sizy; j++)
                {
                    int ii = t.dimx - 1 - ((t.sizx - i - 1) * t.dimx) / t.sizx;
                    int jj = t.dimy - 1 - ((t.sizy - j - 1) * t.dimy) / t.sizy;
                    int ix = jj * t.dimx + ii;
                    char b = pix[ix>>1];
                    if (ix&1)
                        b &= 15;
                    else
                        b = (b >> 4) & 15;
                    data[i*t.sizy+j] = palremap[b];
                }
            }
        }
        Xfree(outbuf);
    }
#endif
}

bool RT_TileLoad(int16_t tilenum)
{
    const int dataOffset = 0xc2270;
    int32_t const tileid = rt_tilemap[tilenum];
    if (tileid < 0)
        return false;
    auto &t = rt_tileinfo[tileid];
    int bufsize = 0;
    if (t.flags & RT_TILE8BIT)
    {
        bufsize = t.dimx * t.dimy;
    }
    else
    {
        bufsize = (t.dimx * t.dimy) / 2 + 32;
    }
    if (rt_waloff[tileid] == 0)
    {
        rt_walock[tileid] = CACHE1D_UNLOCKED;
        g_cache.allocateBlock(&rt_waloff[tileid], bufsize, &rt_walock[tileid]);
    }
    if (!rt_waloff[tileid])
        return false;
    char *inbuf = (char*)Xmalloc(t.filesiz);
    Blseek(rt_group, dataOffset+t.fileoff, SEEK_SET);
    Bread(rt_group, inbuf, t.filesiz);
    if (RNCDecompress(inbuf, (char*)rt_waloff[tileid]) == -1)
    {
        Bmemcpy((char*)rt_waloff[tileid], inbuf, bufsize);
    }
    Xfree(inbuf);

    if (waloff[tilenum])
    {
        char *data = (char*)waloff[tilenum];
        char *src = (char*)rt_waloff[tileid];
        if (t.flags & RT_TILE8BIT)
        {
            for (int i = 0; i < t.sizx; i++)
            {
                for (int j = 0; j < t.sizy; j++)
                {
                    int ii = t.dimx - 1 - ((t.sizx - i - 1) * t.dimx) / t.sizx;
                    int jj = t.dimy - 1 - ((t.sizy - j - 1) * t.dimy) / t.sizy;
                    data[i*t.sizy+j] = src[j*t.dimx+i];
                }
            }
        }
        else
        {
            int palremap[16];
            char *pix = src+32;
            for (int i = 0; i < 16; i++)
            {
                int t = (src[i*2+1] << 8) + src[i*2];
                int r = (t >> 11) & 31;
                int g = (t >> 6) & 31;
                int b = (t >> 1) & 31;
                int a = (t >> 0) & 1;
                r = (r << 3) + (r >> 2);
                g = (g << 3) + (g >> 2);
                b = (b << 3) + (b >> 2);
                if (a == 0)
                    palremap[i] = 255;
                else
                {
                    palremap[i] = paletteGetClosestColor(r, g, b);
                }
            }
            for (int i = 0; i < t.sizx; i++)
            {
                for (int j = 0; j < t.sizy; j++)
                {
                    int ii = t.dimx - 1 - ((t.sizx - i - 1) * t.dimx) / t.sizx;
                    int jj = t.dimy - 1 - ((t.sizy - j - 1) * t.dimy) / t.sizy;
                    int ix = jj * t.dimx + ii;
                    char b = pix[ix>>1];
                    if (ix&1)
                        b &= 15;
                    else
                        b = (b >> 4) & 15;
                    data[i*t.sizy+j] = palremap[b];
                }
            }
        }
    }

    return true;
}
void rt_gloadtile_n64(int32_t dapic, int32_t dapal, int32_t tintpalnum, int32_t dashade, int32_t dameth, pthtyp *pth, int32_t doalloc)
{
    int tileid = rt_tilemap[dapic];
    static int32_t fullbrightloadingpass = 0;
    vec2_16_t const & tsizart = tilesiz[dapic];
    vec2_t siz = { 0, 0 }, tsiz = { 0, 0 };
    int const picdim = tsiz.x*tsiz.y;
    char hasalpha = 0;
    tileinfo_t *tinfo = nullptr;

    if (tileid >= 0)
    {
        tinfo = &rt_tileinfo[tileid];
        tsiz.x = tinfo->dimx;
        tsiz.y = tinfo->dimy;
    }

    if (!glinfo.texnpot)
    {
        for (siz.x = 1; siz.x < tsiz.x; siz.x += siz.x) { }
        for (siz.y = 1; siz.y < tsiz.y; siz.y += siz.y) { }
    }
    else
    {
        if ((tsiz.x|tsiz.y) == 0)
            siz.x = siz.y = 1;
        else
            siz = tsiz;
    }

    coltype *pic = (coltype *)Xmalloc(siz.x*siz.y*sizeof(coltype));

    if (tileid < 0 || !rt_waloff[tileid])
    {
        //Force invalid textures to draw something - an almost purely transparency texture
        //This allows the Z-buffer to be updated for mirrors (which are invalidated textures)
        pic[0].r = pic[0].g = pic[0].b = 0; pic[0].a = 1;
        tsiz.x = tsiz.y = 1; hasalpha = 1;
    }
    else
    {
        int is8bit = (tinfo->flags & RT_TILE8BIT) != 0;
        for (bssize_t y = 0; y < siz.y; y++)
        {
            coltype *wpptr = &pic[y * siz.x];
            int32_t y2 = (y < tsiz.y) ? y : y - tsiz.y;

            for (bssize_t x = 0; x < siz.x; x++, wpptr++)
            {
                int32_t dacol;
                int32_t x2 = (x < tsiz.x) ? x : x-tsiz.x;

                if ((dameth & DAMETH_CLAMPED) && (x >= tsiz.x || y >= tsiz.y)) //Clamp texture
                {
                    wpptr->r = wpptr->g = wpptr->b = wpptr->a = 0;
                    continue;
                }

                if (is8bit)
                {
                    dacol = *(char *)(rt_waloff[tileid]+y2*tsiz.x+x2);
                    dacol = rt_palette[dapal][dacol];
                }
                else
                {
                    int o = y2 * tsiz.x + x2;
                    dacol = *(char *)(rt_waloff[tileid]+32+o/2);
                    if (o&1)
                        dacol &= 15;
                    else
                        dacol >>= 4;
                    if (!(dameth & DAMETH_N64_INTENSIVITY))
                    {
                        dacol = *(uint16_t*)(rt_waloff[tileid]+2*dacol);
                        dacol = B_LITTLE16(dacol);
                    }
                }

                if (dameth & DAMETH_N64_INTENSIVITY)
                {
                    int32_t i = (dacol << 4) | dacol;
                    wpptr->r = wpptr->g = wpptr->b = wpptr->a = i;
                    hasalpha = 1;
                }
                else
                {
                    int32_t r = (dacol >> 11) & 31;
                    int32_t g = (dacol >> 6) & 31;
                    int32_t b = (dacol >> 1) & 31;
                    int32_t a = (dacol >> 0) & 1;

                    wpptr->r = (r << 3) + (r >> 2);
                    wpptr->g = (g << 3) + (g >> 2);
                    wpptr->b = (b << 3) + (b >> 2);

                    if (a == 0)
                    {
                        wpptr->a = 0;
                        hasalpha = 1;
                    }
                    else
                        wpptr->a = 255;
                }

#if 0
                bricolor((palette_t *)wpptr, dacol);

                if (tintpalnum >= 0)
                {
                    polytint_t const & tint = hictinting[tintpalnum];
                    polytintflags_t const effect = tint.f;
                    uint8_t const r = tint.r;
                    uint8_t const g = tint.g;
                    uint8_t const b = tint.b;

                    if (effect & HICTINT_GRAYSCALE)
                    {
                        wpptr->g = wpptr->r = wpptr->b = (uint8_t) ((wpptr->r * GRAYSCALE_COEFF_RED) +
                                                                (wpptr->g * GRAYSCALE_COEFF_GREEN) +
                                                                (wpptr->b * GRAYSCALE_COEFF_BLUE));
                    }

                    if (effect & HICTINT_INVERT)
                    {
                        wpptr->b = 255 - wpptr->b;
                        wpptr->g = 255 - wpptr->g;
                        wpptr->r = 255 - wpptr->r;
                    }

                    if (effect & HICTINT_COLORIZE)
                    {
                        wpptr->b = min((int32_t)((wpptr->b) * b) >> 6, 255);
                        wpptr->g = min((int32_t)((wpptr->g) * g) >> 6, 255);
                        wpptr->r = min((int32_t)((wpptr->r) * r) >> 6, 255);
                    }

                    switch (effect & HICTINT_BLENDMASK)
                    {
                        case HICTINT_BLEND_SCREEN:
                            wpptr->b = 255 - (((255 - wpptr->b) * (255 - b)) >> 8);
                            wpptr->g = 255 - (((255 - wpptr->g) * (255 - g)) >> 8);
                            wpptr->r = 255 - (((255 - wpptr->r) * (255 - r)) >> 8);
                            break;
                        case HICTINT_BLEND_OVERLAY:
                            wpptr->b = wpptr->b < 128 ? (wpptr->b * b) >> 7 : 255 - (((255 - wpptr->b) * (255 - b)) >> 7);
                            wpptr->g = wpptr->g < 128 ? (wpptr->g * g) >> 7 : 255 - (((255 - wpptr->g) * (255 - g)) >> 7);
                            wpptr->r = wpptr->r < 128 ? (wpptr->r * r) >> 7 : 255 - (((255 - wpptr->r) * (255 - r)) >> 7);
                            break;
                        case HICTINT_BLEND_HARDLIGHT:
                            wpptr->b = b < 128 ? (wpptr->b * b) >> 7 : 255 - (((255 - wpptr->b) * (255 - b)) >> 7);
                            wpptr->g = g < 128 ? (wpptr->g * g) >> 7 : 255 - (((255 - wpptr->g) * (255 - g)) >> 7);
                            wpptr->r = r < 128 ? (wpptr->r * r) >> 7 : 255 - (((255 - wpptr->r) * (255 - r)) >> 7);
                            break;
                    }
                }
#endif

                //swap r & b so that we deal with the data as BGRA
                uint8_t tmpR = wpptr->r;
                wpptr->r = wpptr->b;
                wpptr->b = tmpR;
            }
        }
    }

    if (doalloc) glGenTextures(1,(GLuint *)&pth->glpic); //# of textures (make OpenGL allocate structure)
    glBindTexture(GL_TEXTURE_2D, pth->glpic);

    // fixtransparency(pic,tsiz,siz,dameth);

#if 0
    if (polymost_want_npotytex(dameth, siz.y) && tsiz.x == siz.x && tsiz.y == siz.y)  // XXX
    {
        const int32_t nextpoty = 1 << ((picsiz[dapic] >> 4) + 1);
        const int32_t ydif = nextpoty - siz.y;
        coltype *paddedpic;

        Bassert(ydif < siz.y);

        paddedpic = (coltype *)Xrealloc(pic, siz.x * nextpoty * sizeof(coltype));

        pic = paddedpic;
        Bmemcpy(&pic[siz.x * siz.y], pic, siz.x * ydif * sizeof(coltype));
        siz.y = tsiz.y = nextpoty;

        npoty = 1;
    }
#endif

    if (!doalloc)
    {
        vec2_t pthSiz2 = pth->siz;
        if (!glinfo.texnpot)
        {
            for (pthSiz2.x = 1; pthSiz2.x < pth->siz.x; pthSiz2.x += pthSiz2.x) { }
            for (pthSiz2.y = 1; pthSiz2.y < pth->siz.y; pthSiz2.y += pthSiz2.y) { }
        }
        else
        {
            if ((pthSiz2.x|pthSiz2.y) == 0)
                pthSiz2.x = pthSiz2.y = 1;
            else
                pthSiz2 = pth->siz;
        }
        if (siz.x > pthSiz2.x ||
            siz.y > pthSiz2.y)
        {
            //POGO: grow our texture to hold the tile data
            doalloc = true;
        }
    }
    uploadtexture(doalloc, siz, GL_BGRA, pic, tsiz,
                    dameth | DAMETH_ARTIMMUNITY |
                    (dapic >= MAXUSERTILES ? (DAMETH_NOTEXCOMPRESS|DAMETH_NODOWNSIZE) : 0) | /* never process these short-lived tiles */
                    (hasalpha ? (DAMETH_HASALPHA|DAMETH_ONEBITALPHA) : 0));

    Xfree(pic);

    polymost_setuptexture(dameth, -1);

    pth->picnum = dapic;
    pth->palnum = dapal;
    pth->shade = dashade;
    pth->effects = 0;
    pth->flags = PTH_N64 | TO_PTH_CLAMPED(dameth) | TO_PTH_NOTRANSFIX(dameth) | (hasalpha*(PTH_HASALPHA|PTH_ONEBITALPHA)) | TO_PTH_N64_INTENSIVITY(dameth);
    pth->hicr = NULL;
    pth->siz = tsiz;
}

GLuint rt_shaderprogram;
GLuint rt_stexsamplerloc = -1;
GLuint rt_stexcombloc = -1;
GLuint rt_stexcomb = 0;
GLuint rt_scolor1loc = 0;
GLfloat rt_scolor1[4] = {
    0.f, 0.f, 0.f, 0.f
};
GLuint rt_scolor2loc = 0;
GLfloat rt_scolor2[4] = {
    0.f, 0.f, 0.f, 0.f
};

void RT_SetShader(void)
{
    glUseProgram(rt_shaderprogram);
    rt_stexsamplerloc = glGetUniformLocation(rt_shaderprogram, "s_texture");
    rt_stexcombloc = glGetUniformLocation(rt_shaderprogram, "u_texcomb");
    rt_scolor1loc = glGetUniformLocation(rt_shaderprogram, "u_color1");
    rt_scolor2loc = glGetUniformLocation(rt_shaderprogram, "u_color2");
    glUniform1i(rt_stexsamplerloc, 0);
    glUniform1f(rt_stexcombloc, rt_stexcomb);
    glUniform4fv(rt_scolor1loc, 1, rt_scolor1);
    glUniform4fv(rt_scolor2loc, 1, rt_scolor2);
}

void RT_SetColor1(int r, int g, int b, int a)
{
    rt_scolor1[0] = r / 255.f;
    rt_scolor1[1] = g / 255.f;
    rt_scolor1[2] = b / 255.f;
    rt_scolor1[3] = a / 255.f;
    glUniform4fv(rt_scolor1loc, 1, rt_scolor1);
}

void RT_SetColor2(int r, int g, int b, int a)
{
    rt_scolor2[0] = r / 255.f;
    rt_scolor2[1] = g / 255.f;
    rt_scolor2[2] = b / 255.f;
    rt_scolor2[3] = a / 255.f;
    glUniform4fv(rt_scolor2loc, 1, rt_scolor2);
}

void RT_SetTexComb(int comb)
{
    if (rt_stexcomb != comb)
    {
        rt_stexcomb = comb;
        glUniform1f(rt_stexcombloc, rt_stexcomb);
    }
}

void RT_GLInit(void)
{
    if (videoGetRenderMode() == REND_CLASSIC)
        return;
    const char* const RT_VERTEX_SHADER =
        "#version 110\n\
         \n\
         varying vec4 v_color;\n\
         varying float v_distance;\n\
         \n\
         const float c_zero = 0.0;\n\
         const float c_one  = 1.0;\n\
         \n\
         void main()\n\
         {\n\
            vec4 eyeCoordPosition = gl_ModelViewMatrix * gl_Vertex;\n\
            gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n\
            \n\
            eyeCoordPosition.xyz /= eyeCoordPosition.w;\n\
            gl_TexCoord[0] = gl_MultiTexCoord0;\n\
            \n\
            gl_FogFragCoord = abs(eyeCoordPosition.z);\n\
            //gl_FogFragCoord = clamp((gl_Fog.end-abs(eyeCoordPosition.z))*gl_Fog.scale, c_zero, c_one);\n\
            \n\
            v_color = gl_Color;\n\
         }\n\
         ";
    const char* const RT_FRAGMENT_SHADER =
        "#version 110\n\
         #extension GL_ARB_shader_texture_lod : enable\n\
         \n\
         uniform sampler2D s_texture;\n\
         uniform vec4 u_color1;\n\
         uniform vec4 u_color2;\n\
         uniform float u_texcomb;\n\
         \n\
         varying vec4 v_color;\n\
         varying float v_distance;\n\
         \n\
         const float c_zero = 0.0;\n\
         const float c_one  = 1.0;\n\
         const float c_two  = 2.0;\n\
         \n\
         void main()\n\
         {\n\
         #ifdef GL_ARB_shader_texture_lod\n\
            //vec4 color = texture2DGradARB(s_texture, gl_TexCoord[0].xy, dFdx(gl_TexCoord[0].xy), dFdy(gl_TexCoord[0].xy));\n\
            vec4 color = texture2D(s_texture, gl_TexCoord[0].xy);\n\
         #else\n\
            vec2 transitionBlend = fwidth(floor(gl_TexCoord[0].xy));\n\
            transitionBlend = fwidth(transitionBlend)+transitionBlend;\n\
            vec2 texCoord = mix(fract(gl_TexCoord[0].xy), abs(c_one-mod(gl_TexCoord[0].xy+c_one, c_two)), transitionBlend);\n\
            vec4 color = texture2D(s_texture, u_texturePosSize.xy+texCoord);\n\
         #endif\n\
            \n\
            vec4 colorcomb;\n\
            colorcomb.rgb = mix(u_color1.rgb, u_color2.rgb, color.r);\n\
            colorcomb.a = color.a * v_color.a;\n\
            color.rgb = v_color.rgb * color.rgb;\n\
            \n\
            color.a *= v_color.a;\n\
            \n\
            color = mix(color, colorcomb, u_texcomb);\n\
            \n\
            gl_FragData[0] = color;\n\
         }\n\
         ";

    rt_shaderprogram = glCreateProgram();
    GLuint vertexshaderid = polymost2_compileShader(GL_VERTEX_SHADER, RT_VERTEX_SHADER);
    GLuint fragmentshaderid = polymost2_compileShader(GL_FRAGMENT_SHADER, RT_FRAGMENT_SHADER);
    glAttachShader(rt_shaderprogram, vertexshaderid);
    glAttachShader(rt_shaderprogram, fragmentshaderid);
    glLinkProgram(rt_shaderprogram);
}

static float x_vs = 160.f;
static float y_vs = 120.f;
static float x_vt = 160.f;
static float y_vt = 120.f;
static float vp_scale = 1.f;
static float rt_globaldepth;
static int rt_fxtile = 0;

void RT_DisplayTileWorld(float x, float y, float sx, float sy, int16_t picnum, int flags)
{
    int xflip = (flags & 4) != 0;
    int yflip = (flags & 8) != 0;

    sx *= vp_scale;
    sy *= vp_scale;

    float xoff = picanm[picnum].xofs * sx / 6.f;
    if (xflip)
        xoff = -xoff;

    x -= xoff * 2.f;

    float sizx = tilesiz[picnum].x * sx / 6.f;
    float sizy = tilesiz[picnum].y * sy / 6.f;

    if (sizx < 1.f && sizy < 1.f)
        return;

    float x1 = x - sizx;
    float x2 = x + sizx;
    float y1 = y - sizy;
    float y2 = y + sizy;

    float u1, u2, v1, v2;
    if (!xflip)
    {
        u1 = 0.f;
        u2 = 1.f;
    }
    else
    {
        u1 = 1.f;
        u2 = 0.f;
    }

    if (!yflip)
    {
        v1 = 0.f;
        v2 = 1.f;
    }
    else
    {
        v1 = 0.f;
        v2 = 1.f;
    }

    if (!waloff[picnum])
        tileLoad(picnum);
    
    int method = DAMETH_CLAMPED | DAMETH_N64 | (rt_fxtile ? DAMETH_N64_INTENSIVITY : 0);
    pthtyp *pth = texcache_fetch(picnum, 0, 0, method);

    if (!pth)
        return;

    glBindTexture(GL_TEXTURE_2D, pth->glpic);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 320.f, 240.f, 0, -1.f, 1.f);
    glBegin(GL_QUADS);
    glTexCoord2f(u1, v1); glVertex3f(x1, y1, -rt_globaldepth);
    glTexCoord2f(u2, v1); glVertex3f(x2, y1, -rt_globaldepth);
    glTexCoord2f(u2, v2); glVertex3f(x2, y2, -rt_globaldepth);
    glTexCoord2f(u1, v2); glVertex3f(x1, y2, -rt_globaldepth);
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

float rt_sky_color[2][3];

static float rt_globalhoriz;
static float rt_globalposx, rt_globalposy, rt_globalposz;
static float rt_globalang;

void setfxcolor(int a1, int a2, int a3, int a4, int a5, int a6)
{
    rt_fxtile = 1;
    glEnable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    RT_SetColor1(a1, a2, a3, 255);
    RT_SetColor2(a4, a5, a6, 255);
    RT_SetTexComb(1);
}

void unsetfxcolor(void)
{
    rt_fxtile = 0;
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    RT_SetTexComb(0);
}

void RT_DisplaySky(void)
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    rt_globaldepth = 0.f;
    setfxcolor(rt_sky_color[0][0], rt_sky_color[0][1], rt_sky_color[0][2], rt_sky_color[1][0], rt_sky_color[1][1], rt_sky_color[1][2]);
    glDisable(GL_BLEND);
    RT_DisplayTileWorld(x_vt, y_vt + rt_globalhoriz - 100.f, 52.f, 103.f, 3976, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void RT_DisablePolymost()
{
    RT_SetShader();
    RT_SetTexComb(0);
}

void RT_EnablePolymost()
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glDisable(GL_CULL_FACE);
    polymost_resetVertexPointers();
    polymost_setFogEnabled(true);
    polymost_usePaletteIndexing(true);
}

static GLfloat rt_projmatrix[16];


void RT_SetupMatrix(void)
{
    float dx = 512.f * cosf(rt_globalang / (1024.f / fPI));
    float dy = 512.f * sinf(rt_globalang / (1024.f / fPI));
    float dz = -(rt_globalhoriz - 100.f) * 4.f;
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glScalef(0.5f, 0.5f, 0.5f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    bgluPerspective(60.f, 4.f/3.f, 5.f, 16384.f);
    bgluLookAt(rt_globalposx * 0.5f, rt_globalposy * 0.5f, rt_globalposz * 0.5f, (rt_globalposx * 0.5f + dx), (rt_globalposy * 0.5f + dy), (rt_globalposz * 0.5f + dz), 0.f, 0.f, -1.f);
    glGetFloatv(GL_PROJECTION_MATRIX, rt_projmatrix);
}

static int rt_globalpicnum = -1;
static vec2f_t rt_uvscale;

static inline int RT_PicSizLog(int siz)
{
    int lg = 0;
    while (siz > (1 << lg))
        lg++;
    return lg;
}

void RT_SetTexture(int tilenum)
{
    if (rt_globalpicnum == tilenum)
        return;
    
    rt_globalpicnum = tilenum;

    tilenum += animateoffs(tilenum, 0);

    int tileid = rt_tilemap[tilenum];

    int method = DAMETH_N64;
    pthtyp *pth = texcache_fetch(tilenum, 0, 0, method);
    if (pth)
        glBindTexture(GL_TEXTURE_2D, pth->glpic);

    if (tileid >= 0)
    {
        auto& tinfo = rt_tileinfo[tileid];
        int logx = RT_PicSizLog(tinfo.dimx);
        int logy = RT_PicSizLog(tinfo.dimy);
        rt_uvscale.x = 1.f/float(32 << logx);
        rt_uvscale.y = 1.f/float(32 << logy);
    }
    else
    {
        rt_uvscale.x = 1.f;
        rt_uvscale.y = 1.f;
    }
}

int globalcolorred, globalcolorgreen, globalcolorblue;

void RT_CalculateShade(int x, int y, int z, int shade)
{
    if (shade > 126)
        shade = -127;
    shade = (28 - shade) * 9;
    if (shade > 256)
        shade = 256;
    if (shade < 0)
        shade = 0;

    int dx = abs(globalposx/2 - x);
    int dy = abs(globalposy/2 - y);
    globalcolorblue = 256 - ((min(dx, dy) >> 3) + max(dx, dy) + (min(dx, dy) >> 2)) / 60;
    if (shade == 256)
        globalcolorblue = 256;

    int sectnum;
    if (g_player[screenpeek].ps->newowner >= 0)
        sectnum = sprite[g_player[screenpeek].ps->newowner].sectnum;
    else
        sectnum = g_player[screenpeek].ps->cursectnum;

    if (sectnum >= 0 && sector[sectnum].lotag == ST_2_UNDERWATER)
        globalpal = 1;

    globalcolorred = globalcolorblue;
    globalcolorgreen = globalcolorblue;
    switch (globalpal)
    {
    case 1:
        globalcolorred /= 2;
        globalcolorgreen /= 2;
        break;
    case 2:
        globalcolorgreen /= 2;
        globalcolorblue /= 2;
        break;
    case 4:
        globalcolorred = 0;
        globalcolorgreen = 0;
        globalcolorblue = 0;
        break;
    case 6:
    case 8:
    case 14:
        globalcolorred /= 2;
        globalcolorblue /= 2;
        break;
    }

    if (g_player[screenpeek].ps->heat_on)
    {
        globalcolorgreen = (globalcolorgreen * 0x180) >> 8;
        globalcolorred = (globalcolorred * 0xab) >> 8;
        globalcolorblue = (globalcolorblue * 0xab) >> 8;
        shade = (shade + 512) / 3;
    }
    globalcolorred = (globalcolorred * shade) / 256;
    globalcolorgreen = (globalcolorgreen * shade) / 256;
    globalcolorblue = (globalcolorblue * shade) / 256;
    if (globalcolorred < 0)
        globalcolorred = 0;
    if (globalcolorred > 255)
        globalcolorred = 255;
    if (globalcolorgreen < 0)
        globalcolorgreen = 0;
    if (globalcolorgreen > 255)
        globalcolorgreen = 255;
    if (globalcolorblue < 0)
        globalcolorblue = 0;
    if (globalcolorblue > 255)
        globalcolorblue = 255;
}

void RT_DrawCeiling(int sectnum)
{
    auto rt_sect = &rt_sector[sectnum];
    auto sect = &sector[sectnum];
    RT_SetTexComb(0);
    RT_SetTexture(sector[sectnum].ceilingpicnum);
    globalpal = sect->ceilingpal;
    globalshade = sect->ceilingshade;
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < rt_sect->ceilingvertexnum * 3; i++)
    {
        auto vtx = rt_sectvtx[rt_sect->ceilingvertexptr+i];
        float x = vtx.x;
        float y = vtx.y;
        float z = getceilzofslope(sectnum, vtx.x * 2, vtx.y * 2) / 32.f;
        glTexCoord2f(vtx.u * rt_uvscale.x, vtx.v * rt_uvscale.y); 
        RT_CalculateShade(vtx.x, vtx.y, vtx.z, globalshade);
        glColor4f(globalcolorred*(1.f/255.f), globalcolorgreen*(1.f/255.f), globalcolorblue*(1.f/255.f), 1.f);
        glVertex3f(x, y, z);
    }
    glEnd();
}

void RT_DrawFloor(int sectnum)
{
    auto rt_sect = &rt_sector[sectnum];
    auto sect = &sector[sectnum];
    RT_SetTexComb(0);
    int method = DAMETH_N64;
    RT_SetTexture(sector[sectnum].floorpicnum);
    globalpal = sect->floorpal;
    globalshade = sect->floorshade;
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < rt_sect->floorvertexnum * 3; i++)
    {
        auto vtx = rt_sectvtx[rt_sect->floorvertexptr+i];
        float x = vtx.x;
        float y = vtx.y;
        float z = getflorzofslope(sectnum, vtx.x * 2, vtx.y * 2) / 32.f;
        glTexCoord2f(vtx.u * rt_uvscale.x, vtx.v * rt_uvscale.y); 
        RT_CalculateShade(vtx.x, vtx.y, vtx.z, globalshade);
        glColor4f(globalcolorred*(1.f/255.f), globalcolorgreen*(1.f/255.f), globalcolorblue*(1.f/255.f), 1.f);
        glVertex3f(x, y, z);
    }
    glEnd();
}

static rt_vertex_t wallvtx[12];

static int rt_wallcalcres, rt_haswhitewall, rt_hastopwall, rt_hasbottomwall, rt_hasoneway;
static int rt_wallpolycount;

static int globaltileid;
static vec2f_t globaltilescale, globaltilesiz, globaltiledim;

void RT_SetTileGlobals(int tilenum)
{
    int tileid = rt_tilemap[tilenum];
    if (tileid < 0)
        return;
    globaltileid = tileid;
    globaltilescale = { float(rt_tileinfo[tileid].sizx) / float(rt_tileinfo[tileid].dimx),
                        float(rt_tileinfo[tileid].sizy) / float(rt_tileinfo[tileid].dimy) };

    globaltilesiz = { float(rt_tileinfo[tileid].sizx), float(rt_tileinfo[tileid].sizy) };
    globaltiledim = { float(rt_tileinfo[tileid].dimx), float(rt_tileinfo[tileid].dimy) };
}

static float globalxrepeat, globalyrepeat, globalxpanning, globalypanning;
static float globalwallvoffset, globalwallu1, globalwallu2, globalwallv1, globalwallv2, globalwallv3, globalwallv4;

void RT_SetWallGlobals(int wallnum, int cstat)
{
    globalxrepeat = wall[wallnum].xrepeat / globaltilescale.x;
    globalxpanning = wall[wallnum].xpanning / globaltilescale.x;
    globalyrepeat = wall[wallnum].yrepeat / (4.f * globaltilescale.y);
    if (!(cstat & 256))
    {
        globalyrepeat = -globalyrepeat;
        globalypanning = (globaltiledim.y / 256.f) * wall[wallnum].ypanning;
    }
    else
        globalypanning = (globaltiledim.y / 256.f) * (255 - wall[wallnum].ypanning);

    globalwallvoffset = globalypanning * 32.f;
    if (cstat & 4)
        globalwallvoffset += globaltiledim.y * 32.f;

    globalwallu1 = globalxpanning * 32.f;
    globalwallu2 = globalwallu1 + (globalxrepeat * 8.f) * 32.f;
}

void RT_SetWallGlobals2(int wallnum, int cstat)
{
    int nextwall = wall[wallnum].nextwall;
    globalxrepeat = wall[wallnum].xrepeat / globaltilescale.x;
    globalxpanning = wall[nextwall].xpanning / globaltilescale.x;
    globalyrepeat = wall[wallnum].yrepeat / (4.f * globaltilescale.y);
    if (!(cstat & 256))
    {
        globalyrepeat = -globalyrepeat;
        globalypanning = (globaltiledim.y / 256.f) * wall[nextwall].ypanning;
    }
    else
        globalypanning = (globaltiledim.y / 256.f) * (255 - wall[nextwall].ypanning);

    globalwallvoffset = globalypanning * 32.f;
    if (cstat & 4)
        globalwallvoffset += globaltiledim.y * 32.f;

    globalwallu1 = globalxpanning * 32.f;
    globalwallu2 = globalwallu1 + (globalxrepeat * 8.f) * 32.f;
}

void RT_HandleWallCstat(int cstat)
{
    if (cstat & 8)
    {
        float t = globalwallu2;
        globalwallu2 = globalwallu1;
        globalwallu1 = t;
    }
    float v3 = min(globalwallv1, globalwallv2);
    if (v3 < -32760.f)
    {
        float adjust = ((int(fabs(v3) - 32760.f) + 4095) & ~4095);
        globalwallv1 += adjust;
        globalwallv2 += adjust;
        globalwallv3 += adjust;
        globalwallv4 += adjust;
    }
    v3 = max(globalwallv1, globalwallv2);
    if (v3 > 32760.f)
    {
        float adjust = ((int(v3 - 32760.f) + 4095) & ~4095);
        globalwallv1 -= adjust;
        globalwallv2 -= adjust;
        globalwallv3 -= adjust;
        globalwallv4 -= adjust;
    }

}

void RT_HandleWallCstatSlope(int cstat)
{
    if (cstat & 8)
    {
        float t = globalwallu2;
        globalwallu2 = globalwallu1;
        globalwallu1 = t;
    }
    float v3 = min({ globalwallv1, globalwallv2, globalwallv3, globalwallv4 });
    if (v3 < -32760.f)
    {
        float adjust = ((int(v3 - -32760.f) + 4095) & ~4095);
        globalwallv1 += adjust;
        globalwallv2 += adjust;
        globalwallv3 += adjust;
        globalwallv4 += adjust;
    }
    v3 = max({ globalwallv1, globalwallv2, globalwallv3, globalwallv4 });
    if (v3 > 32760.f)
    {
        float adjust = ((int(v3 - 32760.f) + 4095) & ~4095);
        globalwallv1 -= adjust;
        globalwallv2 -= adjust;
        globalwallv3 -= adjust;
        globalwallv4 -= adjust;
    }

}

int RT_WallCalc_NoSlope(int sectnum, int wallnum)
{
    auto &w = wall[wallnum];
    int nextsectnum = w.nextsector;

    int ret = 0;
    rt_wallpolycount = 0;
    if (nextsectnum == -1)
    {
        int z1 = sector[sectnum].ceilingz;
        int z2 = sector[sectnum].floorz;
        int z1s = z1 >> 4;
        int z2s = z2 >> 4;
        if (z1s == z2s)
            return ret;

        int wx1 = w.x;
        int wy1 = w.y;
        int wx2 = wall[w.point2].x;
        int wy2 = wall[w.point2].y;
        int v10 = z1s;
        if (w.cstat & 4)
            v10 = z2s;

        RT_SetTileGlobals(w.picnum);
        RT_SetWallGlobals(wallnum, w.cstat);
        globalwallv1 = (v10 - z1s) * globalyrepeat + globalwallvoffset;
        globalwallv2 = (v10 - z2s) * globalyrepeat + globalwallvoffset;
        RT_HandleWallCstat(w.cstat);
        wallvtx[rt_wallpolycount*4+0].x = wx1 >> 1;
        wallvtx[rt_wallpolycount*4+0].y = wy1 >> 1;
        wallvtx[rt_wallpolycount*4+0].z = z1 >> 5;
        wallvtx[rt_wallpolycount*4+0].u = globalwallu1;
        wallvtx[rt_wallpolycount*4+0].v = globalwallv1;
        wallvtx[rt_wallpolycount*4+1].x = wx1 >> 1;
        wallvtx[rt_wallpolycount*4+1].y = wy1 >> 1;
        wallvtx[rt_wallpolycount*4+1].z = z2 >> 5;
        wallvtx[rt_wallpolycount*4+1].u = globalwallu1;
        wallvtx[rt_wallpolycount*4+1].v = globalwallv2;
        wallvtx[rt_wallpolycount*4+2].x = wx2 >> 1;
        wallvtx[rt_wallpolycount*4+2].y = wy2 >> 1;
        wallvtx[rt_wallpolycount*4+2].z = z2 >> 5;
        wallvtx[rt_wallpolycount*4+2].u = globalwallu2;
        wallvtx[rt_wallpolycount*4+2].v = globalwallv2;
        wallvtx[rt_wallpolycount*4+3].x = wx2 >> 1;
        wallvtx[rt_wallpolycount*4+3].y = wy2 >> 1;
        wallvtx[rt_wallpolycount*4+3].z = z1 >> 5;
        wallvtx[rt_wallpolycount*4+3].u = globalwallu2;
        wallvtx[rt_wallpolycount*4+3].v = globalwallv1;
        rt_wallpolycount++;
        ret |= 8;
        return ret;
    }
    if ((sector[sectnum].ceilingstat&1) == 0 || (sector[nextsectnum].ceilingstat&1) == 0)
    {
        int z1 = sector[sectnum].ceilingz;
        int z2 = sector[nextsectnum].ceilingz;
        int z1s = z1 >> 4;
        int z2s = z2 >> 4;
        if (z1s < z2s)
        {
            int wx1 = w.x;
            int wy1 = w.y;
            int wx2 = wall[w.point2].x;
            int wy2 = wall[w.point2].y;
            int v10;
            int cstat = w.cstat;
            if ((cstat & 4) == 0)
            {
                cstat |= 4;
                v10 = z2s;
            }
            else
            {
                cstat &= ~4;
                v10 = z1s;
            }
            RT_SetTileGlobals(w.picnum);
            RT_SetWallGlobals(wallnum, cstat);
            globalwallv1 = (v10 - z1s) * globalyrepeat + globalwallvoffset;
            globalwallv2 = (v10 - z2s) * globalyrepeat + globalwallvoffset;
            RT_HandleWallCstat(cstat);
            wallvtx[rt_wallpolycount*4+0].x = wx1 >> 1;
            wallvtx[rt_wallpolycount*4+0].y = wy1 >> 1;
            wallvtx[rt_wallpolycount*4+0].z = z1 >> 5;
            wallvtx[rt_wallpolycount*4+0].u = globalwallu1;
            wallvtx[rt_wallpolycount*4+0].v = globalwallv1;
            wallvtx[rt_wallpolycount*4+1].x = wx1 >> 1;
            wallvtx[rt_wallpolycount*4+1].y = wy1 >> 1;
            wallvtx[rt_wallpolycount*4+1].z = z2 >> 5;
            wallvtx[rt_wallpolycount*4+1].u = globalwallu1;
            wallvtx[rt_wallpolycount*4+1].v = globalwallv2;
            wallvtx[rt_wallpolycount*4+2].x = wx2 >> 1;
            wallvtx[rt_wallpolycount*4+2].y = wy2 >> 1;
            wallvtx[rt_wallpolycount*4+2].z = z2 >> 5;
            wallvtx[rt_wallpolycount*4+2].u = globalwallu2;
            wallvtx[rt_wallpolycount*4+2].v = globalwallv2;
            wallvtx[rt_wallpolycount*4+3].x = wx2 >> 1;
            wallvtx[rt_wallpolycount*4+3].y = wy2 >> 1;
            wallvtx[rt_wallpolycount*4+3].z = z1 >> 5;
            wallvtx[rt_wallpolycount*4+3].u = globalwallu2;
            wallvtx[rt_wallpolycount*4+3].v = globalwallv1;
            rt_wallpolycount++;
            ret |= 1;
        }
    }
    if ((sector[sectnum].floorstat&1) == 0 || (sector[nextsectnum].floorstat&1) == 0)
    {
        int z1 = sector[nextsectnum].floorz;
        int z2 = sector[sectnum].floorz;
        int z1s = z1 >> 4;
        int z2s = z2 >> 4;
        if (z1s < z2s)
        {
            int wx1 = w.x;
            int wy1 = w.y;
            int wx2 = wall[w.point2].x;
            int wy2 = wall[w.point2].y;
            int v10;
            int cstat = w.cstat;
            if ((cstat & 2) == 0)
            {
                if (cstat&4)
                    v10 = sector[sectnum].ceilingz>>4;
                else
                    v10 = z1s;
                cstat &= ~4;
                RT_SetTileGlobals(w.picnum);
                RT_SetWallGlobals(wallnum, cstat);
            }
            else
            {
                RT_SetTileGlobals(wall[w.nextwall].picnum);
                if (cstat & 4)
                    v10 = sector[sectnum].ceilingz>>4;
                else
                    v10 = z1s;
                cstat &= ~4;
                RT_SetWallGlobals2(wallnum, cstat);
            }
            globalwallv1 = (v10 - z1s) * globalyrepeat + globalwallvoffset;
            globalwallv2 = (v10 - z2s) * globalyrepeat + globalwallvoffset;
            RT_HandleWallCstat(cstat);
            wallvtx[rt_wallpolycount*4+0].x = wx1 >> 1;
            wallvtx[rt_wallpolycount*4+0].y = wy1 >> 1;
            wallvtx[rt_wallpolycount*4+0].z = z1 >> 5;
            wallvtx[rt_wallpolycount*4+0].u = globalwallu1;
            wallvtx[rt_wallpolycount*4+0].v = globalwallv1;
            wallvtx[rt_wallpolycount*4+1].x = wx1 >> 1;
            wallvtx[rt_wallpolycount*4+1].y = wy1 >> 1;
            wallvtx[rt_wallpolycount*4+1].z = z2 >> 5;
            wallvtx[rt_wallpolycount*4+1].u = globalwallu1;
            wallvtx[rt_wallpolycount*4+1].v = globalwallv2;
            wallvtx[rt_wallpolycount*4+2].x = wx2 >> 1;
            wallvtx[rt_wallpolycount*4+2].y = wy2 >> 1;
            wallvtx[rt_wallpolycount*4+2].z = z2 >> 5;
            wallvtx[rt_wallpolycount*4+2].u = globalwallu2;
            wallvtx[rt_wallpolycount*4+2].v = globalwallv2;
            wallvtx[rt_wallpolycount*4+3].x = wx2 >> 1;
            wallvtx[rt_wallpolycount*4+3].y = wy2 >> 1;
            wallvtx[rt_wallpolycount*4+3].z = z1 >> 5;
            wallvtx[rt_wallpolycount*4+3].u = globalwallu2;
            wallvtx[rt_wallpolycount*4+3].v = globalwallv1;
            rt_wallpolycount++;
            ret |= 2;
        }
    }
    if ((w.cstat & 32) == 0)
    {
        if (w.cstat & 16)
        {
            int wx = abs(globalposx - (w.x + wall[w.point2].x) / 2);
            int wy = abs(globalposy - (w.y + wall[w.point2].y) / 2);
            maskdrawlist[sortspritescnt].dist = (min(wx, wy) >> 3) + max(wx, wy) + (min(wx, wy) >> 2);
            maskdrawlist[sortspritescnt].index = wallnum | 32768;
            sortspritescnt++;
        }
    }
    else
    {
        int wx1 = w.x;
        int wy1 = w.y;
        int wx2 = wall[w.point2].x;
        int wy2 = wall[w.point2].y;
        int z1 = getceilzofslope(nextsectnum, wx1, wy1);
        int z2 = getflorzofslope(nextsectnum, wx1, wy1);
        int v9;
        if (w.cstat & 4)
            v9 = sector[sectnum].ceilingz;
        else
            v9 = sector[nextsectnum].floorz;
        RT_SetTileGlobals(w.overpicnum);
        RT_SetWallGlobals(wallnum, w.cstat & ~4);
        globalwallv1 = ((v9>>4) - (z1>>4)) * globalyrepeat + globalwallvoffset;
        globalwallv2 = ((v9>>4) - (z2>>4)) * globalyrepeat + globalwallvoffset;
        wallvtx[rt_wallpolycount*4+0].x = wx1 >> 1;
        wallvtx[rt_wallpolycount*4+0].y = wy1 >> 1;
        wallvtx[rt_wallpolycount*4+0].z = z1 >> 5;
        wallvtx[rt_wallpolycount*4+0].u = globalwallu1;
        wallvtx[rt_wallpolycount*4+0].v = globalwallv1;
        wallvtx[rt_wallpolycount*4+1].x = wx1 >> 1;
        wallvtx[rt_wallpolycount*4+1].y = wy1 >> 1;
        wallvtx[rt_wallpolycount*4+1].z = z2 >> 5;
        wallvtx[rt_wallpolycount*4+1].u = globalwallu1;
        wallvtx[rt_wallpolycount*4+1].v = globalwallv2;
        wallvtx[rt_wallpolycount*4+2].x = wx2 >> 1;
        wallvtx[rt_wallpolycount*4+2].y = wy2 >> 1;
        wallvtx[rt_wallpolycount*4+2].z = z2 >> 5;
        wallvtx[rt_wallpolycount*4+2].u = globalwallu2;
        wallvtx[rt_wallpolycount*4+2].v = globalwallv2;
        wallvtx[rt_wallpolycount*4+3].x = wx2 >> 1;
        wallvtx[rt_wallpolycount*4+3].y = wy2 >> 1;
        wallvtx[rt_wallpolycount*4+3].z = z1 >> 5;
        wallvtx[rt_wallpolycount*4+3].u = globalwallu2;
        wallvtx[rt_wallpolycount*4+3].v = globalwallv1;
        rt_wallpolycount++;
        ret |= 4;
    }
    return ret;
}

int RT_WallCalc_Slope(int sectnum, int wallnum)
{
    auto &w = wall[wallnum];
    int nextsectnum = w.nextsector;

    int ret = 0;
    rt_wallpolycount = 0;
    if (nextsectnum == -1)
    {
        int wx1 = w.x;
        int wy1 = w.y;
        int wx2 = wall[w.point2].x;
        int wy2 = wall[w.point2].y;
        int z1 = getceilzofslope(sectnum, wx1, wy1);
        int z2 = getflorzofslope(sectnum, wx1, wy1);
        int z3 = getflorzofslope(sectnum, wx2, wy2);
        int z4 = getceilzofslope(sectnum, wx2, wy2);
        if ((z1 >> 4) == (z2 >> 4) && (z3 >> 4) == (z4 >> 4))
            return ret;

        int v2;
        if (w.cstat & 4)
            v2 = sector[sectnum].floorz;
        else
            v2 = sector[sectnum].ceilingz;
        RT_SetTileGlobals(w.picnum);
        RT_SetWallGlobals(wallnum, w.cstat);

        globalwallv1 = ((v2>>4) - (z1>>4)) * globalyrepeat + globalwallvoffset;
        globalwallv2 = ((v2>>4) - (z2>>4)) * globalyrepeat + globalwallvoffset;
        globalwallv3 = ((v2>>4) - (z3>>4)) * globalyrepeat + globalwallvoffset;
        globalwallv4 = ((v2>>4) - (z4>>4)) * globalyrepeat + globalwallvoffset;
        RT_HandleWallCstatSlope(w.cstat);
        wallvtx[rt_wallpolycount*4+0].x = wx1 >> 1;
        wallvtx[rt_wallpolycount*4+0].y = wy1 >> 1;
        wallvtx[rt_wallpolycount*4+0].z = z1 >> 5;
        wallvtx[rt_wallpolycount*4+0].u = globalwallu1;
        wallvtx[rt_wallpolycount*4+0].v = globalwallv1;
        wallvtx[rt_wallpolycount*4+1].x = wx1 >> 1;
        wallvtx[rt_wallpolycount*4+1].y = wy1 >> 1;
        wallvtx[rt_wallpolycount*4+1].z = z2 >> 5;
        wallvtx[rt_wallpolycount*4+1].u = globalwallu1;
        wallvtx[rt_wallpolycount*4+1].v = globalwallv2;
        wallvtx[rt_wallpolycount*4+2].x = wx2 >> 1;
        wallvtx[rt_wallpolycount*4+2].y = wy2 >> 1;
        wallvtx[rt_wallpolycount*4+2].z = z3 >> 5;
        wallvtx[rt_wallpolycount*4+2].u = globalwallu2;
        wallvtx[rt_wallpolycount*4+2].v = globalwallv3;
        wallvtx[rt_wallpolycount*4+3].x = wx2 >> 1;
        wallvtx[rt_wallpolycount*4+3].y = wy2 >> 1;
        wallvtx[rt_wallpolycount*4+3].z = z4 >> 5;
        wallvtx[rt_wallpolycount*4+3].u = globalwallu2;
        wallvtx[rt_wallpolycount*4+3].v = globalwallv4;
        rt_wallpolycount++;
        ret |= 8;
        return ret;
    }
    if ((sector[sectnum].ceilingstat&1) == 0 || (sector[nextsectnum].ceilingstat&1) == 0)
    {
        int wx1 = w.x;
        int wy1 = w.y;
        int wx2 = wall[w.point2].x;
        int wy2 = wall[w.point2].y;
        int z1 = getceilzofslope(sectnum, wx1, wy1);
        int z2 = getceilzofslope(nextsectnum, wx1, wy1);
        int z3 = getceilzofslope(nextsectnum, wx2, wy2);
        int z4 = getceilzofslope(sectnum, wx2, wy2);
        if (((z2>>4) >= (z1>>4) || (z3>>4) >= (z4>>4))
          && (z2>>4) != (z1>>4) || (z3>>4) != (z4>>4))
        {
            int v14 = min(z1>>4, z2>>4);
            int vz4 = min(z3>>4, z4>>4);
            int vz;
            int cstat = w.cstat;
            if (w.cstat & 4)
            {
                cstat &= ~4;
                vz = sector[sectnum].ceilingz;
            }
            else
            {
                cstat |= 4;
                vz = sector[nextsectnum].ceilingz;
            }
            RT_SetTileGlobals(w.picnum);
            RT_SetWallGlobals(wallnum, cstat);
            globalwallv1 = ((vz>>4) - v14) * globalyrepeat + globalwallvoffset;
            globalwallv2 = ((vz>>4) - (z2>>4)) * globalyrepeat + globalwallvoffset;
            globalwallv3 = ((vz>>4) - (z3>>4)) * globalyrepeat + globalwallvoffset;
            globalwallv4 = ((vz>>4) - vz4) * globalyrepeat + globalwallvoffset;
            RT_HandleWallCstatSlope(cstat);
            wallvtx[rt_wallpolycount*4+0].x = wx1 >> 1;
            wallvtx[rt_wallpolycount*4+0].y = wy1 >> 1;
            wallvtx[rt_wallpolycount*4+0].z = v14 >> 1;
            wallvtx[rt_wallpolycount*4+0].u = globalwallu1;
            wallvtx[rt_wallpolycount*4+0].v = globalwallv1;
            wallvtx[rt_wallpolycount*4+1].x = wx1 >> 1;
            wallvtx[rt_wallpolycount*4+1].y = wy1 >> 1;
            wallvtx[rt_wallpolycount*4+1].z = z2 >> 5;
            wallvtx[rt_wallpolycount*4+1].u = globalwallu1;
            wallvtx[rt_wallpolycount*4+1].v = globalwallv2;
            wallvtx[rt_wallpolycount*4+2].x = wx2 >> 1;
            wallvtx[rt_wallpolycount*4+2].y = wy2 >> 1;
            wallvtx[rt_wallpolycount*4+2].z = z3 >> 5;
            wallvtx[rt_wallpolycount*4+2].u = globalwallu2;
            wallvtx[rt_wallpolycount*4+2].v = globalwallv3;
            wallvtx[rt_wallpolycount*4+3].x = wx2 >> 1;
            wallvtx[rt_wallpolycount*4+3].y = wy2 >> 1;
            wallvtx[rt_wallpolycount*4+3].z = vz4 >> 1;
            wallvtx[rt_wallpolycount*4+3].u = globalwallu2;
            wallvtx[rt_wallpolycount*4+3].v = globalwallv4;
            rt_wallpolycount++;
            ret |= 1;
        }
    }
    if ((sector[sectnum].floorstat&1) == 0 || (sector[nextsectnum].floorstat&1) == 0)
    {
        int wx1 = w.x;
        int wy1 = w.y;
        int wx2 = wall[w.point2].x;
        int wy2 = wall[w.point2].y;
        int z1 = getflorzofslope(nextsectnum, wx1, wy1);
        int z2 = getflorzofslope(sectnum, wx1, wy1);
        int z3 = getflorzofslope(sectnum, wx2, wy2);
        int z4 = getflorzofslope(nextsectnum, wx2, wy2);
        if (((z2>>4) >= (z1>>4) || (z3>>4) >= (z4>>4))
          && (z2>>4) != (z1>>4) || (z3>>4) != (z4>>4))
        {
            int v14 = max(z1>>4, z2>>4);
            int vz4 = max(z3>>4, z4>>4);
            int vz;
            int cstat;
            if (w.cstat & 2)
            {
                RT_SetTileGlobals(wall[w.nextwall].picnum);
                if (w.cstat & 4)
                {
                    cstat = w.cstat;
                    vz = sector[sectnum].ceilingz;
                    RT_SetWallGlobals2(wallnum, wall[w.nextwall].cstat & ~4);
                }
                else
                {
                    cstat = w.cstat;
                    vz = sector[nextsectnum].floorz;
                    RT_SetWallGlobals2(wallnum, wall[w.nextwall].cstat & ~4);
                }
            }
            else
            {
                if (w.cstat & 4)
                    vz = sector[sectnum].ceilingz;
                else
                    vz = sector[nextsectnum].floorz;
                cstat = w.cstat & ~4;
                RT_SetTileGlobals(w.picnum);
                RT_SetWallGlobals(wallnum, cstat);
            }
            globalwallv1 = ((vz>>4) - (z1>>4)) * globalyrepeat + globalwallvoffset;
            globalwallv2 = ((vz>>4) - v14) * globalyrepeat + globalwallvoffset;
            globalwallv3 = ((vz>>4) - vz4) * globalyrepeat + globalwallvoffset;
            globalwallv4 = ((vz>>4) - (z4>>4)) * globalyrepeat + globalwallvoffset;
            RT_HandleWallCstatSlope(cstat);
            wallvtx[rt_wallpolycount*4+0].x = wx1 >> 1;
            wallvtx[rt_wallpolycount*4+0].y = wy1 >> 1;
            wallvtx[rt_wallpolycount*4+0].z = z1 >> 5;
            wallvtx[rt_wallpolycount*4+0].u = globalwallu1;
            wallvtx[rt_wallpolycount*4+0].v = globalwallv1;
            wallvtx[rt_wallpolycount*4+1].x = wx1 >> 1;
            wallvtx[rt_wallpolycount*4+1].y = wy1 >> 1;
            wallvtx[rt_wallpolycount*4+1].z = v14 >> 1;
            wallvtx[rt_wallpolycount*4+1].u = globalwallu1;
            wallvtx[rt_wallpolycount*4+1].v = globalwallv2;
            wallvtx[rt_wallpolycount*4+2].x = wx2 >> 1;
            wallvtx[rt_wallpolycount*4+2].y = wy2 >> 1;
            wallvtx[rt_wallpolycount*4+2].z = vz4 >> 1;
            wallvtx[rt_wallpolycount*4+2].u = globalwallu2;
            wallvtx[rt_wallpolycount*4+2].v = globalwallv3;
            wallvtx[rt_wallpolycount*4+3].x = wx2 >> 1;
            wallvtx[rt_wallpolycount*4+3].y = wy2 >> 1;
            wallvtx[rt_wallpolycount*4+3].z = z4 >> 5;
            wallvtx[rt_wallpolycount*4+3].u = globalwallu2;
            wallvtx[rt_wallpolycount*4+3].v = globalwallv4;
            rt_wallpolycount++;
            ret |= 2;
        }
    }
    if ((w.cstat & 32) == 0)
    {
        if (w.cstat & 16)
        {
            int wx = abs(globalposx - (w.x + wall[w.point2].x) / 2);
            int wy = abs(globalposy - (w.y + wall[w.point2].y) / 2);
            maskdrawlist[sortspritescnt].dist = (min(wx, wy) >> 3) + max(wx, wy) + (min(wx, wy) >> 2);
            maskdrawlist[sortspritescnt].index = wallnum | 32768;
            sortspritescnt++;
        }
    }
    else
    {
        int wx1 = w.x;
        int wy1 = w.y;
        int wx2 = wall[w.point2].x;
        int wy2 = wall[w.point2].y;
        int z1 = getceilzofslope(nextsectnum, wx1, wy1);
        int z2 = getflorzofslope(nextsectnum, wx1, wy1);
        int z3 = getflorzofslope(nextsectnum, wx2, wy2);
        int z4 = getceilzofslope(nextsectnum, wx2, wy2);
        int v2;
        if (w.cstat & 4)
            v2 = sector[sectnum].ceilingz;
        else
            v2 = sector[nextsectnum].floorz;
        int cstat = w.cstat & ~4;
        RT_SetTileGlobals(w.overpicnum);
        RT_SetWallGlobals(wallnum, cstat);

        globalwallv1 = ((v2>>4) - (z1>>4)) * globalyrepeat + globalwallvoffset;
        globalwallv2 = ((v2>>4) - (z2>>4)) * globalyrepeat + globalwallvoffset;
        globalwallv3 = ((v2>>4) - (z3>>4)) * globalyrepeat + globalwallvoffset;
        globalwallv4 = ((v2>>4) - (z4>>4)) * globalyrepeat + globalwallvoffset;
        RT_HandleWallCstatSlope(cstat);
        wallvtx[rt_wallpolycount*4+0].x = wx1 >> 1;
        wallvtx[rt_wallpolycount*4+0].y = wy1 >> 1;
        wallvtx[rt_wallpolycount*4+0].z = z1 >> 5;
        wallvtx[rt_wallpolycount*4+0].u = globalwallu1;
        wallvtx[rt_wallpolycount*4+0].v = globalwallv1;
        wallvtx[rt_wallpolycount*4+1].x = wx1 >> 1;
        wallvtx[rt_wallpolycount*4+1].y = wy1 >> 1;
        wallvtx[rt_wallpolycount*4+1].z = z2 >> 5;
        wallvtx[rt_wallpolycount*4+1].u = globalwallu1;
        wallvtx[rt_wallpolycount*4+1].v = globalwallv2;
        wallvtx[rt_wallpolycount*4+2].x = wx2 >> 1;
        wallvtx[rt_wallpolycount*4+2].y = wy2 >> 1;
        wallvtx[rt_wallpolycount*4+2].z = z3 >> 5;
        wallvtx[rt_wallpolycount*4+2].u = globalwallu2;
        wallvtx[rt_wallpolycount*4+2].v = globalwallv3;
        wallvtx[rt_wallpolycount*4+3].x = wx2 >> 1;
        wallvtx[rt_wallpolycount*4+3].y = wy2 >> 1;
        wallvtx[rt_wallpolycount*4+3].z = z4 >> 5;
        wallvtx[rt_wallpolycount*4+3].u = globalwallu2;
        wallvtx[rt_wallpolycount*4+3].v = globalwallv4;
        rt_wallpolycount++;
        ret |= 4;
    }
    return ret;
}

int RT_WallCalc(int sectnum, int wallnum)
{
    int cstat = sector[sectnum].floorstat | sector[sectnum].ceilingstat;
    int nextsectnum = wall[wallnum].nextsector;
    if (nextsectnum != -1)
        cstat |= sector[nextsectnum].floorstat | sector[nextsectnum].ceilingstat;
    if (cstat & 2)
        return RT_WallCalc_Slope(sectnum, wallnum);
    return RT_WallCalc_NoSlope(sectnum, wallnum);
}

static tspritetype rt_tsprite, *rt_tspriteptr;

static int rt_tspritetileid, rt_tspritepicnum;
static int rt_fxcolor;
static int rt_curfxcolor, rt_boss2, rt_lastpicnum, rt_spritezbufferhack;
static float viewangsin, viewangcos;
static vec2_t rt_spritedim;
static int rt_spritedimtotal;

void RT_SetupDrawMask(void)
{
    rt_lastpicnum = 0;
    rt_boss2 = 0;
    rt_spritezbufferhack = 0;
    rt_fxtile = 0;
    rt_curfxcolor = 0;
    viewangcos = cosf(rt_globalang * BANG2RAD);
    viewangsin = sinf(rt_globalang * BANG2RAD);
    RT_SetTexComb(0);

    glEnable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
}

static vec3_t colortable[14][2] = {
    { 0, 0, 0, 0, 0, 0 },
    { 255, 255, 0, 255, 0, 0 },
    { 255, 255, 255, 255, 255, 0 },
    { 128, 128, 255, 64, 64, 128 },
    { 15, 255, 255, 115, 0, 170 },
    { 128, 128, 128, 64, 64, 64 },
    { 0, 0, 255, 64, 64, 128 },
    { 255, 192, 192, 64, 64, 64 },
    { 0, 0, 0, 0, 0, 0 },
    { 255, 0, 0, 255, 0, 0 },
    { 0, 255, 0, 0, 255, 255 },
    { 255, 0, 0, 255, 127, 0 },
    { 255, 255, 255, 64, 64, 64 },
    { 255, 255, 255, 0, 127, 127 }
};

static int rt_tspritesect;
static uint8_t rt_globalalpha;

void RT_DrawSpriteFace(float x, float y, float z, int pn)
{
    if (rt_tspriteptr->xrepeat == 0 || rt_tspriteptr->yrepeat == 0)
        return;

    float sw = rt_projmatrix[15] + rt_projmatrix[3] * x + rt_projmatrix[7] * y + rt_projmatrix[11] * z;
    if (sw == 0.f)
        return;
    float sx = (rt_projmatrix[12] + rt_projmatrix[0] * x + rt_projmatrix[4] * y + rt_projmatrix[8] * z) / sw;
    float sy = (rt_projmatrix[13] + rt_projmatrix[1] * x + rt_projmatrix[5] * y + rt_projmatrix[9] * z) / sw;
    float sz = (rt_projmatrix[14] + rt_projmatrix[2] * x + rt_projmatrix[6] * y + rt_projmatrix[10] * z) / sw;
    if (sx < -2 || sx > 2.f || sy < -2.f || sy > 2.f || sz < 0.f || sz > 1.f)
        return;
    
    rt_globaldepth = sz;

    float tt = 1.f - sz;

    glColor4f(globalcolorred * (1.f / 255.f), globalcolorgreen * (1.f / 255.f), globalcolorblue * (1.f / 255.f), rt_globalalpha * (1.f / 255.f));
    RT_DisplayTileWorld(sx * x_vs + x_vt, -sy * y_vs + y_vt, rt_tspriteptr->xrepeat * tt * 4.f, rt_tspriteptr->yrepeat * tt * 4.f,
        rt_tspritepicnum, rt_tspriteptr->cstat);
    //RT_DisplayTileWorld(sx * x_vs + x_vt, -sy * y_vs + y_vt, 4.f, 4.f,
    //    rt_tspriteptr->picnum, rt_tspriteptr->cstat);
}

void RT_DrawSpriteFlat(int spritenum, int sectnum, int distance)
{
    rt_lastpicnum = 0;
    globalpal = rt_tspriteptr->pal;
    RT_CalculateShade(rt_tspriteptr->x/2, rt_tspriteptr->y/2,rt_tspriteptr->z/2, rt_tspriteptr->shade);
    int xoff = int8_t((rt_tileinfo[rt_tspritetileid].picanm>>8)&255);
    int yoff = int8_t((rt_tileinfo[rt_tspritetileid].picanm>>16)&255);
    int v20 = (rt_tileinfo[rt_tspritetileid].sizx * rt_tspriteptr->xrepeat) / 16;
    int v11 = (rt_tileinfo[rt_tspritetileid].sizy * rt_tspriteptr->yrepeat) / 8;
    int v6 = ((xoff + rt_tspriteptr->xoffset) * rt_tspriteptr->xrepeat) / 8;
    if (rt_tspriteptr->cstat&128)
        rt_tspriteptr->z += (v11 >> 1) * 32;
    if (rt_tspriteptr->cstat&8) {
    }

    rt_tspriteptr->z += (yoff + rt_tspriteptr->yoffset) * rt_tspriteptr->yrepeat * -4;

    int sz = (rt_tspriteptr->z >> 5) - v11;
    float v1, v2;
    if (rt_tspriteptr->cstat&4)
    {
        v1 = 0.f;
        v2 = 1.f;
    }
    else
    {
        v1 = 1.f;
        v2 = 0.f;
    }

    rt_globalalpha = 255;
    if (rt_tspriteptr->cstat&2)
        rt_globalalpha = 192;
    if (rt_tspriteptr->cstat&512)
        rt_globalalpha = 128;

    int16_t v40 = (rt_tspriteptr->x / 2.f);
    int16_t v48 = (rt_tspriteptr->y / 2.f);
    int16_t v46 = v48;
    int16_t v3e = v40;

    if((rt_tspriteptr->cstat&48)==0)
    {
        switch(sprite[spritenum].picnum)
        {
        case 0xa0:
        case 0x21a:
        case 0x220:
        case 0x23d:
        case 0x244:
        case 0x245:
        case 0x246:
        case 0x247:
        case 0x25f:
        case 0x260:
        case 0x267:
        case 0x268:
        case 0x269:
        case 0x26a:
        case 0x26b:
        case 0x279:
        case 0x27a:
        case 0x27b:
        case 0x27c:
        case 0x27d:
        case 700:
        case 0x38c:
        case 0x38d:
        case 0x38e:
        case 0x398:
        case 0x399:
        case 0x3ac:
        case 0x3b6:
        case 0x3ce:
        case 0x3cf:
        case 0x3d1:
        case 0x3df:
        case 0x3e1:
        case 0x3e2:
        case 0x3e3:
        case 0x3e4:
        case 0x3e5:
        case 0x3ed:
        case 0x444:
        case 0x4c5:
        case 0x4c9:
        case 0x4ec:
        case 0x8df:
        case 0x8fc:
        case 0x905:
        case 0x907:
        case 0xd65:
        case 0x1119:
        case 0x11c6:
        case 0x11e7:
        case 0x11ea:
        case 0x11eb:
        case 0x131a:
            break;
        default:
            RT_DrawSpriteFace(v40/2, v48/2, (sz + (v11>>1))/2, rt_tspritepicnum);
            return;
        }
        float ds = viewangsin * v20;
        float dc = viewangcos * v20;
        v40 += ds;
        v3e -= ds;
        v46 += dc;
        v48 -= dc;
    }
    else if((rt_tspriteptr->cstat&48)==16)
    {
        float ang = rt_tspriteptr->ang / (1024.f/180.f);
        if (rt_spritezbufferhack)
        {
            int zoff = distance / 120;
            if (zoff < 4)
                zoff = 4;
            float zs = sin((ang+90.f)/(180.f/fPI));
            float zc = cos((ang+90.f)/(180.f/fPI));
            v3e += zs * zoff;
            v46 -= zc * zoff;
        }
        int o1 = v3e, o2 = v46;
        float fs = sin((ang-180.f)/(180.f/fPI));
        float fc = cos((ang-180.f)/(180.f/fPI));
        v40 = o1 + fs * v20 + fs * v6;
        v3e = o1 - fs * v20 + fs * v6;
        v46 = o2 + fc * v20;
        v48 = o2 - fc * v20;
    }

    int sz2;
    if (rt_tspriteptr->cstat & 8)
    {
        sz2 = sz;
        sz += v11;
    }
    else
    {
        sz2 = sz + v11;
    }
    
    int method = DAMETH_CLAMPED | DAMETH_N64 | (rt_fxtile ? DAMETH_N64_INTENSIVITY : 0);
    pthtyp *pth = texcache_fetch(rt_tspritepicnum, 0, 0, method);

    if (!pth)
        return;

    glBindTexture(GL_TEXTURE_2D, pth->glpic);
    //rt_globalalpha = 128;
    glColor4f(globalcolorred * (1.f / 255.f), globalcolorgreen * (1.f / 255.f), globalcolorblue * (1.f / 255.f), rt_globalalpha * (1.f / 255.f));
    glBegin(GL_QUADS);
    glTexCoord2f(v1, 0.f); glVertex3f(v3e, v46, sz);
    glTexCoord2f(v2, 0.f); glVertex3f(v40, v48, sz);
    glTexCoord2f(v2, 1.f); glVertex3f(v40, v48, sz2);
    glTexCoord2f(v1, 1.f); glVertex3f(v3e, v46, sz2);
    glEnd();
}

void RT_DrawSpriteFloor(void)
{
    float u1, v1, u2, v2;
    if ((rt_tspriteptr->cstat&8) == 0)
    {
        v1 = 1.f;
        v2 = 0.f;
    }
    else
    {
        v1 = 0.f;
        v2 = 1.f;
    }
    if ((rt_tspriteptr->cstat&4) == 0)
    {
        u1 = 1.f;
        u2 = 0.f;
    }
    else
    {
        u1 = 0.f;
        u2 = 1.f;
    }

    uint8_t alpha;
    if ((rt_tspriteptr->cstat & 2) == 0)
        alpha = 255;
    else
    {
        if (rt_tspriteptr->cstat & 512)
            alpha = 128;
        else
            alpha = 192;
    }

    globalpal = rt_tspriteptr->pal;

    RT_CalculateShade(rt_tspriteptr->x/2, rt_tspriteptr->y/2,rt_tspriteptr->z/2, rt_tspriteptr->shade);

    int sx = (rt_tileinfo[rt_tspritetileid].sizx * rt_tspriteptr->xrepeat) / 16;
    int sy = (rt_tileinfo[rt_tspritetileid].sizy * rt_tspriteptr->yrepeat) / 16;

    float ang = (rt_tspriteptr->ang / (1024.f/180.f) + 90.f) / (180.f/fPI);
    float ds = sin(ang);
    float dc = cos(ang);

    float x = rt_tspriteptr->x / 2;
    float y = rt_tspriteptr->y / 2;
    float z;
    if (rt_spritezbufferhack)
    {
        int dz = abs(globalposz - rt_tspriteptr->z);
        z = (-rt_tspriteptr->z)>>5;
        if (rt_tspriteptr->z == sector[rt_tspritesect].ceilingz)
            z -= (dz + 16) / 1024;
        if (rt_tspriteptr->z == sector[rt_tspritesect].floorz)
            z += (dz + 16) / 1024;

    }
    else
        z = (-rt_tspriteptr->z)>>5;
    
    int method = DAMETH_CLAMPED | DAMETH_N64 | (rt_fxtile ? DAMETH_N64_INTENSIVITY : 0);
    pthtyp *pth = texcache_fetch(rt_tspritepicnum, 0, 0, method);

    if (!pth)
        return;

    glBindTexture(GL_TEXTURE_2D, pth->glpic);

    glColor4f(globalcolorred * (1.f / 255.f), globalcolorgreen * (1.f / 255.f), globalcolorblue * (1.f / 255.f), alpha * (1.f / 255.f));

    glBegin(GL_QUADS);
    glTexCoord2f(u1, v2); glVertex3f(x + sx * dc + sy * ds, y - sy * dc + sx * ds, -z);
    glTexCoord2f(u2, v2); glVertex3f(x - sx * dc + sy * ds, y - sy * dc - sx * ds, -z);
    glTexCoord2f(u2, v1); glVertex3f(x - sx * dc - sy * ds, y + sy * dc - sx * ds, -z);
    glTexCoord2f(u1, v1); glVertex3f(x + sx * dc - sy * ds, y + sy * dc + sx * ds, -z);
    glEnd();
}

void RT_DrawSprite(int spritenum, int sectnum, int distance)
{
    int pn;
    if (sprite[spritenum].cstat & 32768)
        return;
    if (sprite[spritenum].picnum < 11)
        return;
    if (sprite[spritenum].xrepeat == 0)
        return;

    //rt_tspriteptr = &rt_tsprite;

    //Bmemcpy(&rt_tsprite, &sprite[spritenum], sizeof(spritetype));

    spritesortcnt = 0;
    rt_tspriteptr = renderAddTSpriteFromSprite(spritenum);
    G_DoSpriteAnimations(globalposx, globalposy, globalposz, fix16_to_int(globalang), rt_smoothRatio);

    //// TODO: animatesprites
    //// HACK:
    //if ((rt_tsprite.picnum == 1405))
    //    return;
    if (rt_tspriteptr->xrepeat == 0)
        return;

    rt_tspritepicnum = rt_tspriteptr->picnum;
    rt_tspritetileid = rt_tilemap[rt_tspritepicnum];
    if (rt_tspritetileid == 1)
        return;
    if (rt_tileinfo[rt_tspritetileid].picanm & 192)
    {
        int anim = animateoffs(rt_tspritepicnum, 0);;
        rt_tspritetileid += anim;
        rt_tspritepicnum += anim;
    }

    pn = sprite[spritenum].picnum;

    // TODO: BOSS2 code

    rt_fxcolor = 0;
    if (pn == 1360 || pn == 1671)
    {
        rt_tspriteptr->cstat |= 512;
    }
    switch (pn)
    {
    case 1261:
        rt_fxcolor = 4;
        break;
    case 0x659:
        rt_fxcolor = 2;
        break;
    case 0x65e:
        rt_fxcolor = 7;
        break;
    case 0x66e:
        rt_fxcolor = 10;
        break;
    case 0x678:
        rt_fxcolor = 10;
        break;
    case 0x687:
        rt_fxcolor = 4;
        break;
    case 0x762:
        rt_fxcolor = 1;
        break;
    case 0x8de:
        rt_fxcolor = 1;
        break;
    case 0x8df:
        rt_fxcolor = 1;
        break;
    case 0x906:
        rt_fxcolor = 1;
        break;
    case 0x907:
        rt_fxcolor = 1;
        break;
    case 0x919:
        rt_fxcolor = 5;
        break;
    case 0x990:
        rt_fxcolor = 11;
        break;
    case 0xa07:
        rt_fxcolor = 9;
        break;
    case 0xa23:
        rt_fxcolor = 2;
        break;
    case 0xa24:
        rt_fxcolor = 2;
        break;
    case 0xa25:
        rt_fxcolor = 2;
        break;
    case 0xa26:
        rt_fxcolor = 2;
        break;
    case 0xa27:
        rt_fxcolor = 2;
        break;
    case 0xe8c:
        rt_fxcolor = 2;
        break;
    case 0xf01:
        rt_fxcolor = 4;
        break;
    case 0xf05:
        rt_fxcolor = 2;
        break;
    case 0xf71:
        rt_fxcolor = 13;
        break;
    }
    if (rt_fxcolor == 0)
    {
        if (rt_fxtile)
        {
            RT_SetTexComb(0);
            glEnable(GL_ALPHA_TEST);
            rt_fxtile = 0;
            glColor4f(1.f, 1.f, 1.f, 1.f);
        }
    }
    else
    {
        if (!rt_fxtile)
        {
            glDisable(GL_ALPHA_TEST);
            RT_SetTexComb(1);
            rt_fxtile = 1;
        }
        RT_SetColor1(colortable[rt_fxcolor][1].x, colortable[rt_fxcolor][1].y, colortable[rt_fxcolor][1].z, 255);
        RT_SetColor2(colortable[rt_fxcolor][0].x, colortable[rt_fxcolor][0].y, colortable[rt_fxcolor][0].z, 255);
    }
    rt_spritezbufferhack = (rt_tspriteptr->cstat & 16384) != 0;
    rt_spritedim = { rt_tileinfo[rt_tspritetileid].dimx, rt_tileinfo[rt_tspritetileid].dimy };
    rt_spritedimtotal = rt_spritedim.x * rt_spritedim.y;
    rt_tspritesect = sectnum;
    if ((rt_tspriteptr->cstat & 48) == 16 && (rt_tspriteptr->cstat&64))
    {
        int ang = getangle(rt_tspriteptr->x - globalposx, rt_tspriteptr->y - globalposy) - rt_tspriteptr->ang;
        if (ang > 1024)
            ang -= 2048;
        if (ang < -1024)
            ang += 2048;
        if (klabs(ang) < 512)
            return;
    }
    if ((rt_tspriteptr->cstat&48) == 32)
    {
        if (rt_tspriteptr->cstat & 64)
        {
            if ((rt_tspriteptr->cstat & 8) == 0)
            {
                if (rt_tspriteptr->z < globalposz)
                    return;
            }
            else
            {
                if (rt_tspriteptr->z > globalposz)
                    return;
            }
        }
        if (rt_spritedimtotal <= 4096)
            RT_DrawSpriteFloor();
        return;
    }
    RT_DrawSpriteFlat(spritenum, rt_tspritesect, distance);
}

void RT_DrawWall(int wallnum)
{
    rt_wallcalcres = RT_WallCalc(rt_wall[wallnum].sectnum, wallnum);
    globalpal = wall[wallnum].pal;
    globalshade = wall[wallnum].shade;
    rt_haswhitewall = (rt_wallcalcres & 8) != 0;
    rt_hastopwall = (rt_wallcalcres & 1) != 0;
    rt_hasbottomwall = (rt_wallcalcres & 2) != 0;
    rt_hasoneway = (rt_wallcalcres & 4) != 0;
    int j = 0;
    RT_SetTexture(wall[wallnum].picnum);
    glBegin(GL_QUADS);
    for (int i = 0; i < (rt_haswhitewall + rt_hastopwall) * 4; i++)
    {
        auto vtx = wallvtx[j++];
        glTexCoord2f(vtx.u * rt_uvscale.x, vtx.v * rt_uvscale.y);
        RT_CalculateShade(vtx.x, vtx.y, vtx.z, globalshade);
        glColor4f(globalcolorred*(1.f/255.f), globalcolorgreen*(1.f/255.f), globalcolorblue*(1.f/255.f), 1.f);
        glVertex3f(vtx.x, vtx.y, vtx.z);
    }
    glEnd();
    if (wall[wallnum].cstat & 2)
        RT_SetTexture(wall[wall[wallnum].nextwall].picnum);
    glBegin(GL_QUADS);
    for (int i = 0; i < rt_hasbottomwall * 4; i++)
    {
        auto vtx = wallvtx[j++];
        glTexCoord2f(vtx.u * rt_uvscale.x, vtx.v * rt_uvscale.y);
        RT_CalculateShade(vtx.x, vtx.y, vtx.z, globalshade);
        glColor4f(globalcolorred*(1.f/255.f), globalcolorgreen*(1.f/255.f), globalcolorblue*(1.f/255.f), 1.f);
        glVertex3f(vtx.x, vtx.y, vtx.z);
    }
    glEnd();
    if (rt_hasoneway)
    {
        RT_SetTexture(wall[wallnum].overpicnum);
        glBegin(GL_QUADS);
        for (int i = 0; i < 4; i++)
        {
            auto vtx = wallvtx[j++];
            glTexCoord2f(vtx.u * rt_uvscale.x, vtx.v * rt_uvscale.y);
            RT_CalculateShade(vtx.x, vtx.y, vtx.z, globalshade);
            glColor4f(globalcolorred*(1.f/255.f), globalcolorgreen*(1.f/255.f), globalcolorblue*(1.f/255.f), 1.f);
            glVertex3f(vtx.x, vtx.y, vtx.z);
        }
        glEnd();
    }
}

uint8_t viswalltbit[(MAXWALLS+7)>>3];
uint8_t wallbitcheck[(MAXWALLS+7)>>3];
uint8_t floorbitcheck[(MAXSECTORS+7)>>3];
uint8_t ceilingbitcheck[(MAXSECTORS+7)>>3];
uint8_t vissectbit1[(MAXSECTORS+7)>>3];

int viswallcnt;
int drawallcnt;
int drawceilcnt;
int drawfloorcnt;
int visiblesectornum;

float viswallr1[MAXWALLS], viswallr2[MAXWALLS];
int viswall[MAXWALLS];

float getanglef2(float x1, float y1, float x2, float y2)
{
    return 270.f - RT_GetAngle(y2 - y1, x2 - x1) * (180.f / fPI);
}

float getangledelta(float a1, float a2)
{
    float delta;
    if (a1 > a2)
    {
        delta = a1 - a2;
        if (delta > 180.f)
            delta -= 360.f;
        return -delta;
    }
    delta = a2 - a1;
    if (delta > 180.f)
        delta -= 360.f;
    return delta;
}

int viswallcheck(int w, float f1, float f2)
{
    for (int i = 0; i < viswallcnt; i++)
    {
        if (viswall[i] == w)
        {
            if (getangledelta(f1, viswallr1[i]) <= 0.f && getangledelta(f2, viswallr2[i]) >= 0.f)
                return 0;
        }
    }
    return 1;
}

void RT_ScanSector(float lx, float rx, int sectnum)
{
    if (sector[sectnum].floorheinum == 0)
    {
        if (globalposz < sector[sectnum].floorz)
            floorbitcheck[sectnum>>3] |= pow2char[sectnum&7];
    }
    else
        floorbitcheck[sectnum>>3] |= pow2char[sectnum&7];
    if (sector[sectnum].ceilingheinum == 0)
    {
        if (globalposz> sector[sectnum].ceilingz)
            ceilingbitcheck[sectnum>>3] |= pow2char[sectnum&7];
    }
    else
        ceilingbitcheck[sectnum>>3] |= pow2char[sectnum&7];

    vissectbit1[sectnum>>3] |= pow2char[sectnum&7];

    float x1 = globalposx + sin(lx * (fPI/180.f)) * 5000.f;
    float y1 = globalposy + cos(lx * (fPI/180.f)) * 5000.f;
    float x2 = globalposx + sin(rx * (fPI/180.f)) * 5000.f;
    float y2 = globalposy + cos(rx * (fPI/180.f)) * 5000.f;
    int oviswalcnt = viswallcnt;
    int startwall = sector[sectnum].wallptr;
    int endwall = startwall + sector[sectnum].wallnum;
    for (int w = startwall; w < endwall; w++)
    {
        if (wallbitcheck[w>>3]&pow2char[w&7])
            continue;

        float wx1 = wall[w].x;
        float wy1 = wall[w].y;
        float wx2 = wall[wall[w].point2].x;
        float wy2 = wall[wall[w].point2].y;
        // Side check
        if ((wx1 - globalposx) * (wy2 - globalposy) < (wx2 - globalposx) * (wy1 - globalposy))
        {
            wallbitcheck[w>>3] |= pow2char[w&7];
            continue;
        }
        //// Visibility check
        if ((globalposx - wx1) * (y2 - wy1) < (globalposy - wy1) * (x2 - wx1)
         || (globalposy - wy1) * (x1 - wx1) <= (globalposx - wx1) * (y1 - wy1))
        {
            if ((globalposx - wx2) * (y2 - wy2) < (globalposy - wy2) * (x2 - wx2)
             || (globalposy - wy2) * (x1 - wx2) <= (globalposx - wx2) * (y1 - wy2))
            {
                if ((globalposx - wx1) * (y2 - wy1) >= (globalposy - wy1) * (x2 - wx1)
                    || (globalposy - wy1) * (x1 - wx1) <= (globalposx - wx1) * (y1 - wy1)
                    || (globalposx - wx2) * (y2 - wy2) < (globalposy - wy2) * (x2 - wx2)
                    || (globalposy - wy2) * (x1 - wx2) > (globalposx - wx2) * (y1 - wy2))
                    continue;
            }
        }
        viswalltbit[w>>3] |= pow2char[w&7];
        int nextsectnum = wall[w].nextsector;
        if (nextsectnum == -1)
        {
            wallbitcheck[w>>3] |= pow2char[w&7];
            continue;
        }
        if (sector[nextsectnum].floorz == sector[nextsectnum].ceilingz
            && sector[nextsectnum].ceilingheinum == sector[nextsectnum].floorheinum)
        {
            wallbitcheck[w>>3] |= pow2char[w&7];
            continue;
        }
        float wa2 = getanglef2(wx2, wy2, globalposx, globalposy);
        float wa1 = getanglef2(wx1, wy1, globalposx, globalposy);
        float a2 = lx;
        float d2 = getangledelta(lx, wa2);
        if (d2 >= 0.f)
            a2 = wa2;
        float a1 = rx;
        float d1 = getangledelta(rx, wa1);
        if (d1 <= 0.f)
            a1 = wa1;
        if (d1 <= 0 && d2 >= 0)
        {
            wallbitcheck[w>>3] |= pow2char[w&7];
        }
        if (viswallcheck(w, a2, a1) && viswallcnt < MAXWALLS)
        {
            viswall[viswallcnt] = w;
            viswallr1[viswallcnt] = a2;
            viswallr2[viswallcnt] = a1;
            viswallcnt++;
            // if (viswallcnt > 511)
            //     return;
        }
    }
    for (int i = oviswalcnt; i < viswallcnt; i++)
    {
        RT_ScanSector(viswallr1[i], viswallr2[i], wall[viswall[i]].nextsector);
    }
}

int drawalllist[MAXWALLS];
int drawfloorlist[MAXSECTORS];
int drawceilinglist[MAXSECTORS];
int visiblesectors[MAXSECTORS];

void RT_ScanSectors(int sectnum)
{
    float viewhorizang = RT_GetAngle(rt_globalhoriz - 100.f, 128.f) * (-180.f / fPI);
    float viewrange = fabs(viewhorizang) * (29.f / 46.f) + 35.f;
    float viewangle = RT_AngleMod(-rt_globalang / (1024.f/180.f) + 90.f);
    float viewangler1 = viewangle + viewrange;
    float viewangler2 = viewangle - viewrange;
    
    drawallcnt = 0;
    drawceilcnt = 0;
    drawfloorcnt = 0;
    viswallcnt = 0;
    visiblesectornum = 0;
    memset(viswalltbit, 0, sizeof(viswalltbit));
    memset(wallbitcheck, 0, sizeof(wallbitcheck));
    memset(floorbitcheck, 0, sizeof(floorbitcheck));
    memset(ceilingbitcheck, 0, sizeof(ceilingbitcheck));
    memset(vissectbit1, 0, sizeof(vissectbit1));

    RT_ScanSector(viewangler2, viewangler1, sectnum);
    for (int i = 0; i < ((MAXWALLS+7)>>3); i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (viswalltbit[i] & pow2char[j])
            {
                drawalllist[drawallcnt++] = i * 8 + j;
            }
        }
    }
    for (int i = 0; i < ((MAXSECTORS+7)>>3); i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (floorbitcheck[i] & pow2char[j])
            {
                drawfloorlist[drawfloorcnt++] = i * 8 + j;
            }
        }
    }
    for (int i = 0; i < ((MAXSECTORS+7)>>3); i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (ceilingbitcheck[i] & pow2char[j])
            {
                drawceilinglist[drawceilcnt++] = i * 8 + j;
            }
        }
    }
    for (int i = 0; i < ((MAXSECTORS+7)>>3); i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (vissectbit1[i] & pow2char[j])
            {
                visiblesectors[visiblesectornum++] = i * 8 + j;
            }
        }
    }
    visiblesectors[visiblesectornum] = -1;
    drawceilinglist[drawceilcnt] = -1;
    drawfloorlist[drawfloorcnt] = -1;
    drawalllist[drawallcnt] = -1;
}

void RT_DrawMaskWall(int wallnum)
{
    walltype &w = wall[wallnum];
    int tileid = rt_tilemap[wall[wallnum].overpicnum];
    if (tileid == -1)
        return;
    
    if (rt_fxtile)
    {
        RT_SetTexComb(0);
        glEnable(GL_ALPHA_TEST);
        rt_fxtile = 0;
        glColor4f(1.f, 1.f, 1.f, 1.f);
    }

    if (rt_spritezbufferhack)
        rt_spritezbufferhack = 0;

    int wx1 = w.x;
    int wy1 = w.y;
    int wx2 = wall[w.point2].x;
    int wy2 = wall[w.point2].y;

    int z1 = getceilzofslope(rt_wall[wallnum].sectnum, wx1, wy1);
    int z2 = getceilzofslope(rt_wall[wallnum].sectnum, wx2, wy2);
    int z3 = getceilzofslope(w.nextsector, wx1, wy1);
    int z4 = getceilzofslope(w.nextsector, wx2, wy2);

    int l20;
    int l14;
    if ((z1 >> 4) > (z3 >> 4))
    {
        l14 = z1 >> 4;
        l20 = z2 >> 4;
    }
    else
    {
        l14 = z3 >> 4;
        l20 = z4 >> 4;
    }


    int z5 = getflorzofslope(rt_wall[wallnum].sectnum, wx1, wy1);
    int z6 = getflorzofslope(rt_wall[wallnum].sectnum, wx2, wy2);
    int z7 = getflorzofslope(w.nextsector, wx1, wy1);
    int z8 = getflorzofslope(w.nextsector, wx2, wy2);

    int l18, l1c;
    if ((z5 >> 4) < (z7 >> 4))
    {
        l18 = z5 >> 4;
        l1c = z6 >> 4;
    }
    else
    {
        l18 = z7 >> 4;
        l1c = z8 >> 4;
    }

    int v4;

    if (w.cstat & 4)
    {
        int t1 = sector[w.nextsector].floorz >> 4;
        int t2 = sector[rt_wall[wallnum].sectnum].floorz >> 4;
        if (t2 < t1)
            v4 = t2;
        else
            v4 = t1;
    }
    else
    {
        int t1 = sector[rt_wall[wallnum].sectnum].ceilingz >> 4;
        int t2 = sector[w.nextsector].ceilingz >> 4;
        if (t2 > t1)
            v4 = t2;
        else
            v4 = t1;
    }

    RT_SetTileGlobals(w.overpicnum);
    RT_SetWallGlobals(wallnum, w.cstat & ~4);
    globalwallv1 = (v4 - l14) * globalyrepeat + globalwallvoffset;
    globalwallv2 = (v4 - l18) * globalyrepeat + globalwallvoffset;
    globalwallv3 = (v4 - l1c) * globalyrepeat + globalwallvoffset;
    globalwallv4 = (v4 - l20) * globalyrepeat + globalwallvoffset;
    RT_HandleWallCstatSlope(w.cstat & ~4);
    wallvtx[0].x = wx1 >> 1;
    wallvtx[0].y = wy1 >> 1;
    wallvtx[0].z = l14 >> 1;
    wallvtx[0].u = globalwallu1;
    wallvtx[0].v = globalwallv1;
    wallvtx[1].x = wx1 >> 1;
    wallvtx[1].y = wy1 >> 1;
    wallvtx[1].z = l18 >> 1;
    wallvtx[1].u = globalwallu1;
    wallvtx[1].v = globalwallv2;
    wallvtx[2].x = wx2 >> 1;
    wallvtx[2].y = wy2 >> 1;
    wallvtx[2].z = l1c >> 1;
    wallvtx[2].u = globalwallu2;
    wallvtx[2].v = globalwallv3;
    wallvtx[3].x = wx2 >> 1;
    wallvtx[3].y = wy2 >> 1;
    wallvtx[3].z = l20 >> 1;
    wallvtx[3].u = globalwallu2;
    wallvtx[3].v = globalwallv4;

    int alpha;

    if ((w.cstat & 128) == 0)
        alpha = 255;
    else
    {
        if (w.cstat & 512)
            alpha = 64;
        else
            alpha = 128;
    }

    globalpal = w.pal;

    int pn = w.overpicnum;

    if (rt_tileinfo[tileid].picanm & 192)
    {
        int anim = animateoffs(w.overpicnum, 0);
        tileid += anim;
        pn += anim;
    }

    int method = DAMETH_N64;
    pthtyp* pth = texcache_fetch(pn, 0, 0, method);
    if (pth)
        glBindTexture(GL_TEXTURE_2D, pth->glpic);

    if (tileid >= 0)
    {
        auto& tinfo = rt_tileinfo[tileid];
        int logx = RT_PicSizLog(tinfo.dimx);
        int logy = RT_PicSizLog(tinfo.dimy);
        rt_uvscale.x = 1.f / float(32 << logx);
        rt_uvscale.y = 1.f / float(32 << logy);
    }
    else
    {
        rt_uvscale.x = 1.f;
        rt_uvscale.y = 1.f;
    }
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++)
    {
        auto vtx = wallvtx[i];
        glTexCoord2f(vtx.u * rt_uvscale.x, vtx.v * rt_uvscale.y);
        RT_CalculateShade(vtx.x, vtx.y, vtx.z, w.shade);
        glColor4f(globalcolorred*(1.f/255.f), globalcolorgreen*(1.f/255.f), globalcolorblue*(1.f/255.f), alpha*(1.f/255.f));
        glVertex3f(vtx.x, vtx.y, vtx.z);
    }
    glEnd();
}

void RT_DrawMasks(void)
{
    RT_SetupDrawMask();

    for (int i = 0; i < visiblesectornum; i++)
    {
        int sect = visiblesectors[i];
        for (int j = headspritesect[sect]; j >= 0; j = nextspritesect[j])
        {
            if (sprite[j].cstat & 32768)
                continue;
            if (sprite[j].picnum < 11)
                continue;
            if (sprite[j].xrepeat == 0)
                continue;
            int wx = abs(globalposx - sprite[j].x);
            int wy = abs(globalposy - sprite[j].y);
            int dist = (min(wx, wy) >> 3) + max(wx, wy) + (min(wx, wy) >> 2);

            if (sortspritescnt == 10240)
                continue;

            maskdrawlist[sortspritescnt].dist = dist;
            maskdrawlist[sortspritescnt].index = j;
            maskdrawlist[sortspritescnt].sectnum = sect;
            sortspritescnt++;
        }
    }

    for (int gap = sortspritescnt >> 1; gap; gap >>= 1)
        for (int i = 0; i < sortspritescnt - gap; i++)
            for (int j = i; j >= 0; j -= gap)
            {
                if (maskdrawlist[j].dist < maskdrawlist[j+gap].dist)
                {
                    int t = maskdrawlist[j].dist;
                    maskdrawlist[j].dist = maskdrawlist[j+gap].dist;
                    maskdrawlist[j+gap].dist = t;
                    t = maskdrawlist[j].index;
                    maskdrawlist[j].index = maskdrawlist[j+gap].index;
                    maskdrawlist[j+gap].index = t;
                    t = maskdrawlist[j].sectnum;
                    maskdrawlist[j].sectnum = maskdrawlist[j+gap].sectnum;
                    maskdrawlist[j+gap].sectnum = t;
                }
            }

    for (int i = 0; i < sortspritescnt; i++)
    {
        auto &ms = maskdrawlist[i];
        if (ms.index & 32768)
        {
            RT_DrawMaskWall(ms.index & ~32768);
        }
        else
        {
            RT_DrawSprite(ms.index, ms.sectnum, ms.dist);
        }
    }

    glDepthMask(GL_TRUE);
}

void RT_DrawRooms(int x, int y, int z, fix16_t ang, fix16_t horiz, int16_t sectnum, int smoothRatio)
{
    updatesector(x, y, &sectnum);

    if (sectnum < 0)
    {
        return;
    }

    RT_DisablePolymost();
#if 0
    // Test code
    int32_t method = 0;
    pthtyp* testpth = texcache_fetch(26, 0, 0, method);
    glBindTexture(GL_TEXTURE_2D, testpth->glpic);
    glViewport(0, 0, xdim, ydim);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 320.f, 240.f, 0, -1.f, 1.f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glColor3f(1, 1, 1);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(1, 0); glVertex2f(96.f, 0);
    glTexCoord2f(1, 1); glVertex2f(96.f, 40.f);
    glTexCoord2f(0, 1); glVertex2f(0, 40.f);
    glEnd();
#endif
    
    glDisable(GL_ALPHA_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    globalposx = x;
    globalposy = y;
    globalposz = z;
    globalang = ang;
    rt_smoothRatio = smoothRatio;

    rt_globalpicnum = -1;
    rt_globalposx = x * 0.5f;
    rt_globalposy = y * 0.5f;
    rt_globalposz = z * (1.f/32.f);
    rt_globalhoriz = fix16_to_float(horiz);
    rt_globalang = fix16_to_float(ang);
    RT_SetupMatrix();
    RT_DisplaySky();
    rt_fxcolor = 0;
    sortspritescnt = 0;

    glColor4f(1.f, 1.f, 1.f, 1.f);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);

    RT_ScanSectors(sectnum);

    for (int i = 0; i < drawceilcnt; i++)
    {
        RT_DrawCeiling(drawceilinglist[i]);
    }

    for (int i = 0; i < drawfloorcnt; i++)
    {
        RT_DrawFloor(drawfloorlist[i]);
    }

    for (int i = 0; i < drawallcnt; i++)
    {
        //if (rt_wall[i].sectnum != sectnum)
        //    continue;
        RT_DrawWall(drawalllist[i]);
    }

    //for (int i = 0; i < numsectors; i++)
    //{
    //    RT_DrawCeiling(i);
    //    RT_DrawFloor(i);
    //}
    //
    //for (int i = 0; i < numwalls; i++)
    //{
    //    //if (rt_wall[i].sectnum != sectnum)
    //    //    continue;
    //    RT_DrawWall(i);
    //}

    RT_DrawMasks();

    //for (int i = 0; i < MAXSPRITES; i++)
    //{
    //    if (sprite[i].statnum != MAXSTATUS)
    //    {
    //        int wx = abs(globalposx - sprite[i].x);
    //        int wy = abs(globalposy - sprite[i].y);
    //        RT_DrawSprite(i, sprite[i].sectnum, (min(wx, wy) >> 3) + max(wx, wy) + (min(wx, wy) >> 2));
    //    }
    //}

    RT_EnablePolymost();
}

int ms_x, ms_y, ms_angle;
int ms_list[40], ms_listvtxptr[40];
int ms_list_cnt, ms_vtx_cnt;
int ms_dx[1024], ms_dy[1024];

void RT_MS_Reset(void)
{
    ms_list_cnt = 0;
    ms_vtx_cnt = 0;
    memset(ms_list, -1, sizeof(ms_list));
    memset(ms_listvtxptr, -1, sizeof(ms_listvtxptr));
    memset(ms_dx, -1, sizeof(ms_dx));
    memset(ms_dy, -1, sizeof(ms_dy));
}

static void RT_MS_Add_(int sectnum)
{
    for (int i = 0; i < 40; i++)
    {
        if (ms_list[i] == sectnum)
            return;
    }
    int vc = 0;
    rt_vertex_t *vptr;
    if (sector[sectnum].ceilingstat&64)
    {
        vc += rt_sector[sectnum].ceilingvertexnum * 3;
        vptr = &rt_sectvtx[rt_sector[sectnum].ceilingvertexptr];
    }
    if (sector[sectnum].floorstat&64)
    {
        vc += rt_sector[sectnum].floorvertexnum * 3;
        vptr = &rt_sectvtx[rt_sector[sectnum].floorvertexptr];
    }
    if (ms_list_cnt >= 40)
        return;
    ms_list[ms_list_cnt] = sectnum;
    ms_listvtxptr[ms_list_cnt] = ms_vtx_cnt;
    ms_list_cnt++;

    for (int i = 0; i < vc; i++)
    {
        if (ms_vtx_cnt >= 1024)
            return;
        ms_dx[ms_vtx_cnt] = vptr[i].x - ms_x;
        ms_dy[ms_vtx_cnt] = vptr[i].y - ms_y;
        ms_vtx_cnt++;
    }
}

void RT_MS_Add(int sectnum, int x, int y)
{
    ms_x = x/2;
    ms_y = y/2;

    RT_MS_Add_(sectnum);

    int lotag = sector[sectnum].lotag;
    if (lotag)
    {
        int startwall = sector[sectnum].wallptr;
        int endwall = startwall+sector[sectnum].wallnum;
        for (int i = startwall; i < endwall; i++)
        {
            int nextsectnum = wall[i].nextsector;
            if (nextsectnum != -1 && sector[nextsectnum].lotag == lotag)
            {
                RT_MS_Add_(nextsectnum);
            }
        }
    }
}

static void RT_MS_Update_(int sectnum)
{
    int vc = 0;
    rt_vertex_t *vptr;
    if (sector[sectnum].ceilingstat&64)
    {
        vc += rt_sector[sectnum].ceilingvertexnum * 3;
        vptr = &rt_sectvtx[rt_sector[sectnum].ceilingvertexptr];
    }
    if (sector[sectnum].floorstat&64)
    {
        vc += rt_sector[sectnum].floorvertexnum * 3;
        vptr = &rt_sectvtx[rt_sector[sectnum].floorvertexptr];
    }
    for (int i = 0; i < 40; i++)
    {
        if (ms_list[i] == sectnum)
        {
            for (int j = 0; j < vc; j++)
            {
                vec2_t pivot = { 0, 0 };
                vec2_t p = { ms_dx[ms_listvtxptr[i]+j]<<1, ms_dy[ms_listvtxptr[i]+j]<<1 };
                vec2_t po;
                rotatepoint(pivot, p, ms_angle & 2047, &po);

                vptr[j].x = (ms_x + po.x) / 2;
                vptr[j].y = (ms_y + po.y) / 2;
            }
            return;
        }
    }
}

void RT_MS_Update(int sectnum, int ang, int x, int y)
{
    ms_x = x;
    ms_y = y;
    ms_angle = ang;

    RT_MS_Update_(sectnum);

    int lotag = sector[sectnum].lotag;
    if (lotag)
    {
        int startwall = sector[sectnum].wallptr;
        int endwall = startwall+sector[sectnum].wallnum;
        for (int i = startwall; i < endwall; i++)
        {
            int nextsectnum = wall[i].nextsector;
            if (nextsectnum != -1 && sector[nextsectnum].lotag == lotag)
            {
                RT_MS_Update_(nextsectnum);
            }
        }
    }
}
