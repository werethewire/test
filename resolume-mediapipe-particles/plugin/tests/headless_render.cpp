// Headless render check.
//
// Creates an OpenGL 4.1 core context through EGL (no window, no X server),
// runs the real ParticleSystem against a synthetic walking figure and writes
// the composited frame to a PPM. It is how the GPU path is verified on a
// machine that has neither Resolume nor a GPU: shaders compile on a real
// driver, framebuffers come back complete, and the frame is not empty.
//
// Linux only, and off by default:
//   cmake -S plugin -B build -DMPP_BUILD_HEADLESS_TEST=ON
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless        # whole body
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless alt    # trails + attract
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless two    # two bodies + depth
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless resize # particle count change
#define GLEW_STATIC
#include <GL/glew.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "ParticleSystem.h"
#include "PoseTracker.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static const int W = 320, H = 180;

static void FillPose( float* lm, float t, float shiftX = 0.0f, float depth = 0.0f, float scale = 1.0f )
{
    for( int i = 0; i < mpp::NUM_LANDMARKS; ++i ) { lm[i*4+0]=0.5f; lm[i*4+1]=0.5f; lm[i*4+2]=depth; lm[i*4+3]=1.f; }
    auto set=[&](int i,float x,float y){
        lm[i*4+0] = 0.5f + ( x - 0.5f ) * scale + shiftX;
        lm[i*4+1] = 0.5f + ( y - 0.5f ) * scale;
    };
    float swing = std::sin(t*3.0f)*0.12f;
    set(mpp::LM_NOSE,0.50f,0.15f); set(mpp::LM_LEFT_EAR,0.46f,0.16f); set(mpp::LM_RIGHT_EAR,0.54f,0.16f);
    set(mpp::LM_LEFT_SHOULDER,0.42f,0.30f); set(mpp::LM_RIGHT_SHOULDER,0.58f,0.30f);
    set(mpp::LM_LEFT_ELBOW,0.30f,0.30f+swing); set(mpp::LM_RIGHT_ELBOW,0.70f,0.30f-swing);
    set(mpp::LM_LEFT_WRIST,0.18f,0.30f+swing*2.f); set(mpp::LM_RIGHT_WRIST,0.82f,0.30f-swing*2.f);
    set(mpp::LM_LEFT_HIP,0.45f,0.58f); set(mpp::LM_RIGHT_HIP,0.55f,0.58f);
    set(mpp::LM_LEFT_KNEE,0.45f,0.76f); set(mpp::LM_RIGHT_KNEE,0.55f,0.76f);
    set(mpp::LM_LEFT_ANKLE,0.45f,0.93f); set(mpp::LM_RIGHT_ANKLE,0.55f,0.93f);
    set(mpp::LM_LEFT_FOOT,0.42f,0.97f); set(mpp::LM_RIGHT_FOOT,0.58f,0.97f);
}

int main( int argc, char** argv )
{
    // No X server here: go through the surfaceless / device EGL platforms and
    // render entirely into FBOs.
    EGLDisplay dpy = EGL_NO_DISPLAY;
    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress( "eglGetPlatformDisplayEXT" );
    if( getPlatformDisplay )
        dpy = getPlatformDisplay( EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr );
    EGLint maj, min;
    if( dpy == EGL_NO_DISPLAY || !eglInitialize( dpy, &maj, &min ) )
    {
        auto queryDevices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress( "eglQueryDevicesEXT" );
        EGLDeviceEXT devices[ 8 ];
        EGLint numDevices = 0;
        if( queryDevices && queryDevices( 8, devices, &numDevices ) )
        {
            for( int i = 0; i < numDevices && dpy == EGL_NO_DISPLAY; ++i )
            {
                EGLDisplay candidate = getPlatformDisplay( EGL_PLATFORM_DEVICE_EXT, devices[ i ], nullptr );
                if( candidate != EGL_NO_DISPLAY && eglInitialize( candidate, &maj, &min ) )
                    dpy = candidate;
            }
        }
        if( dpy == EGL_NO_DISPLAY ) { printf( "no usable EGL display\n" ); return 2; }
    }
    printf( "EGL %d.%d\n", maj, min );

    EGLint cfgAttribs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                            EGL_NONE };
    EGLConfig cfg; EGLint n = 0;
    if( !eglChooseConfig( dpy, cfgAttribs, &cfg, 1, &n ) || n == 0 ) { printf("no config\n"); return 2; }
    eglBindAPI( EGL_OPENGL_API );
    EGLint ctxAttribs[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1,
                            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
    EGLContext ctx = eglCreateContext( dpy, cfg, EGL_NO_CONTEXT, ctxAttribs );
    if( ctx == EGL_NO_CONTEXT ) { printf("no 4.1 core context\n"); return 2; }
    if( !eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx ) ) { printf("makeCurrent failed\n"); return 2; }

    glewExperimental = GL_TRUE;
    GLenum glewStatus = glewInit();
    // GLEW_ERROR_NO_GLX_DISPLAY is expected without an X server; the GL entry
    // points are still resolved.
    if( glewStatus != GLEW_OK && glewStatus != 4 ) { printf("glewInit failed: %d\n", glewStatus); return 2; }
    while( glGetError() != GL_NO_ERROR ) {}
    printf("GL %s | %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

    // Stand in for Resolume's host FBO.
    GLuint hostTex, hostFbo;
    glGenTextures(1,&hostTex); glBindTexture(GL_TEXTURE_2D,hostTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenFramebuffers(1,&hostFbo); glBindFramebuffer(GL_FRAMEBUFFER,hostFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,hostTex,0);
    if( glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE ){ printf("host fbo incomplete\n"); return 2; }

    mpp::ParticleSystem ps;
    if( !ps.Init() ) { printf("ParticleSystem::Init failed\n"); return 1; }
    ps.SetTextureSize(128);
    printf("particles: %d\n", ps.ParticleCount());

    mpp::PoseTracker tracker;
    tracker.SetSmoothing(0.3f);
    tracker.SetMirror(false);

    mpp::ParticleParams params;
    params.turbulence = 0.8f;
    params.pointSize  = 6.0f;
    params.inherit    = 1.2f;
    params.lifeSeconds = 1.5f;
    const char* mode = argc > 1 ? argv[1] : "";
    const bool twoBodies = std::strcmp( mode, "two" ) == 0;
    if( twoBodies )
    {
        // Two bodies at different depths: the near one should render larger
        // and brighter, and the emission budget should split between them.
        params.depth      = 1.6f;
        params.pointSize  = 5.0f;
        params.turbulence = 0.4f;
        params.drag       = 2.2f;
        params.lifeSeconds = 1.0f;
    }
    // Second configuration exercises the trail, attract, limbs-only and
    // colour-by-speed branches that the default run does not touch.
    if( std::strcmp( mode, "alt" ) == 0 )
    {
        params.trails    = 0.6f;
        params.attract   = 2.5f;
        params.emitMode  = mpp::EMIT_LIMBS;
        params.colorMode = 1;
        params.gravity   = 0.4f;
        params.drag      = 0.6f;
        tracker.SetEmitMode( mpp::EMIT_LIMBS );
    }

    const float dt = 1.0f/60.0f;
    float t = 0.0f;
    int nonZeroPixels = 0;
    std::vector<unsigned char> pixels(W*H*4);

    const bool resizeTest = std::strcmp( mode, "resize" ) == 0;
    int litBeforeResize = -1;
    int litAfterResize  = -1;

    auto countLit = [ & ]() {
        glBindFramebuffer( GL_FRAMEBUFFER, hostFbo );
        glReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
        int lit = 0;
        for( size_t i = 0; i < pixels.size(); i += 4 )
            if( pixels[i] || pixels[i+1] || pixels[i+2] ) ++lit;
        return lit;
    };

    for( int frame = 0; frame < 90; ++frame )
    {
        mpp::PoseUpdate update;
        update.frames[0].present = true;
        update.fresh[0] = true;
        if( twoBodies )
        {
            // Body 0 is near and on the left, body 1 is far and on the right.
            FillPose(update.frames[0].lm, t, -0.20f, -0.45f, 1.00f);
            update.frames[1].present = true;
            update.fresh[1] = true;
            FillPose(update.frames[1].lm, t * 1.3f, 0.22f, 0.45f, 0.70f);
        }
        else
        {
            FillPose(update.frames[0].lm, t);
        }
        tracker.Update(&update, dt);
        t += dt;
        ps.DrawFrame(tracker, params, dt, t, W, H, hostFbo);
        GLenum err = glGetError();
        if( err != GL_NO_ERROR ) { printf("GL error 0x%x at frame %d\n", err, frame); return 1; }

        if( resizeTest && frame == 60 )
        {
            // Change the particle count mid flight, the way riding the
            // Particles fader does, and check the image survives it.
            litBeforeResize = countLit();
            ps.SetTextureSize( 256 );
            printf("resized 128 -> %d (%d particles)\n", ps.TextureSize(), ps.ParticleCount());
        }
        else if( resizeTest && frame == 61 )
        {
            litAfterResize = countLit();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, hostFbo);
    glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for( size_t i=0;i<pixels.size();i+=4 ) if( pixels[i]||pixels[i+1]||pixels[i+2] ) ++nonZeroPixels;
    printf("lit pixels: %d / %d (%.1f%%)  bodies=%d\n", nonZeroPixels, W*H,
           100.0*nonZeroPixels/(W*H), tracker.ActiveBodies());

    // Write a PPM (flipped, since GL origin is bottom-left).
    FILE* f = fopen( twoBodies ? "headless_two.ppm" : ( argc > 1 ? "headless_alt.ppm" : "headless_out.ppm" ), "wb" );
    fprintf(f,"P6\n%d %d\n255\n",W,H);
    for(int y=H-1;y>=0;--y) for(int x=0;x<W;++x){ const unsigned char* p=&pixels[(y*W+x)*4]; fwrite(p,1,3,f); }
    fclose(f);

    bool ok = nonZeroPixels > 200;
    printf("%s\n", ok ? "PASS: particles rendered" : "FAIL: frame is empty");

    if( resizeTest )
    {
        // A full reset would park every particle off screen, so the frame
        // straight after the resize would collapse toward black.
        printf("lit before resize: %d, one frame after: %d\n",
               litBeforeResize, litAfterResize);
        const bool survived = litAfterResize > litBeforeResize / 2;
        printf("%s\n", survived ? "PASS: particles survived the resize"
                                 : "FAIL: the resize wiped the frame");
        ok = ok && survived;
    }

    ps.DeInit();
    return ok ? 0 : 1;
}
