// Headless render check.
//
// Creates an OpenGL 4.1 core context through EGL (no window, no X server),
// runs the real ParticleSystem against a synthetic walking figure and writes
// the composited frame to a PPM. It is how the GPU path is verified on a
// machine that has neither Resolume nor a GPU: shaders compile on a real
// driver, framebuffers come back complete, and the frame is not empty.
//
// Off by default:
//   cmake -S plugin -B build -DMPP_BUILD_HEADLESS_TEST=ON
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless        # whole body
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless alt    # trails + attract
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless two    # two bodies + depth
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless resize # particle count change
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless hands  # Emit From = Hands
//   LIBGL_ALWAYS_SOFTWARE=1 ./build/mpp_headless head   # Emit From = Head
//
// On Windows it creates the context on a hidden window instead of EGL, which
// runs the shaders on the real GPU driver Resolume would use.
#ifndef GLEW_STATIC
	#define GLEW_STATIC
#endif
#if defined( _WIN32 )
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <GL/glew.h>
	#include <GL/wglew.h>
#else
	#include <GL/glew.h>
	#include <EGL/egl.h>
	#include <EGL/eglext.h>
#endif

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
    // Face and fingers, for the Head and Hands modes. The fingers follow the wrists.
    set(mpp::LM_LEFT_EYE_INNER,0.49f,0.135f); set(mpp::LM_LEFT_EYE,0.48f,0.135f); set(mpp::LM_LEFT_EYE_OUTER,0.47f,0.135f);
    set(mpp::LM_RIGHT_EYE_INNER,0.51f,0.135f); set(mpp::LM_RIGHT_EYE,0.52f,0.135f); set(mpp::LM_RIGHT_EYE_OUTER,0.53f,0.135f);
    set(mpp::LM_MOUTH_LEFT,0.485f,0.18f); set(mpp::LM_MOUTH_RIGHT,0.515f,0.18f);
    float ly = 0.30f+swing*2.f, ry = 0.30f-swing*2.f;
    set(mpp::LM_LEFT_PINKY,0.14f,ly+0.03f); set(mpp::LM_LEFT_INDEX,0.13f,ly); set(mpp::LM_LEFT_THUMB,0.15f,ly-0.03f);
    set(mpp::LM_RIGHT_PINKY,0.86f,ry+0.03f); set(mpp::LM_RIGHT_INDEX,0.87f,ry); set(mpp::LM_RIGHT_THUMB,0.85f,ry-0.03f);
}

#if defined( _WIN32 )
static LRESULT CALLBACK HiddenWindowProc( HWND w, UINT m, WPARAM wp, LPARAM lp ) { return DefWindowProcA( w, m, wp, lp ); }

/// A 4.1 core context on a hidden window: a legacy context first, only to
/// resolve wglCreateContextAttribsARB, then the real one.
static bool CreateHiddenCoreContext()
{
    HINSTANCE instance = GetModuleHandleA( nullptr );
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC; wc.lpfnWndProc = HiddenWindowProc; wc.hInstance = instance; wc.lpszClassName = "mpp_headless";
    if( !RegisterClassA( &wc ) ) return false;
    HWND window = CreateWindowExA( 0, wc.lpszClassName, wc.lpszClassName, WS_OVERLAPPEDWINDOW, 0, 0, 16, 16,
                                   nullptr, nullptr, instance, nullptr );
    if( !window ) return false;
    HDC dc = GetDC( window );
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof( pfd ); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32;
    int format = ChoosePixelFormat( dc, &pfd );
    if( format == 0 || !SetPixelFormat( dc, format, &pfd ) ) return false;
    HGLRC legacy = wglCreateContext( dc );
    if( !legacy || !wglMakeCurrent( dc, legacy ) ) return false;
    glewExperimental = GL_TRUE;
    if( glewInit() != GLEW_OK || !wglewIsSupported( "WGL_ARB_create_context" ) ) return false;
    const int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 1,
                            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
    HGLRC core = wglCreateContextAttribsARB( dc, nullptr, attribs );
    if( !core ) return false;
    wglMakeCurrent( dc, core );
    wglDeleteContext( legacy );
    glewExperimental = GL_TRUE;
    return glewInit() == GLEW_OK;
}
#endif

int main( int argc, char** argv )
{
#if defined( _WIN32 )
    if( !CreateHiddenCoreContext() ) { printf( "no 4.1 core context\n" ); return 2; }
    while( glGetError() != GL_NO_ERROR ) {}
#else
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
#endif
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
    // Hands / Head: emission restricted to the detail bones, with Body
    // Attract on so the shader's limited attract range is exercised too.
    const bool handsTest = std::strcmp( mode, "hands" ) == 0;
    const bool headTest  = std::strcmp( mode, "head" ) == 0;
    if( handsTest || headTest )
    {
        params.emitMode    = handsTest ? mpp::EMIT_HANDS : mpp::EMIT_HEAD;
        params.attract     = 2.0f;
        params.drag        = 2.5f;
        params.turbulence  = 0.3f;
        params.inherit     = 0.6f;
        params.pointSize   = 4.0f;
        tracker.SetEmitMode( mpp::EmitMode( params.emitMode ) );
    }

    const float dt = 1.0f/60.0f;
    float t = 0.0f;
    int nonZeroPixels = 0;
    std::vector<unsigned char> pixels(W*H*4);

    const bool resizeTest  = std::strcmp( mode, "resize" ) == 0;
    const bool hostileTest = std::strcmp( mode, "hostile" ) == 0;
    int litBeforeResize = -1;
    int litAfterResize  = -1;

    auto readHost = [ & ]() {
        glBindFramebuffer( GL_FRAMEBUFFER, hostFbo );
        glReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
    };
    auto countLit = [ & ]() {
        readHost();
        int lit = 0;
        for( size_t i = 0; i < pixels.size(); i += 4 )
            if( pixels[i] || pixels[i+1] || pixels[i+2] ) ++lit;
        return lit;
    };
    auto sumRed = [ & ]() {
        readHost();
        long long total = 0;
        for( size_t i = 0; i < pixels.size(); i += 4 ) total += pixels[i];
        return total;
    };

    // The state a host might plausibly leave behind: a tight scissor box,
    // culling with reversed winding, a colour mask that drops red and blue,
    // and depth/stencil tests that reject everything. A plugin that does not
    // guard against these renders clipped, miscoloured or black inside
    // Resolume while looking perfect in a test harness.
    auto applyHostileState = []() {
        glEnable( GL_SCISSOR_TEST ); glScissor( 0, 0, 4, 4 );
        glEnable( GL_CULL_FACE ); glCullFace( GL_BACK ); glFrontFace( GL_CW );
        glColorMask( GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE );
        glEnable( GL_DEPTH_TEST ); glDepthFunc( GL_NEVER );
        glEnable( GL_STENCIL_TEST ); glStencilFunc( GL_NEVER, 0, 0xFF );
    };
    auto clearHostileState = []() {
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_CULL_FACE ); glFrontFace( GL_CCW );
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glDisable( GL_DEPTH_TEST ); glDepthFunc( GL_LESS );
        glDisable( GL_STENCIL_TEST );
    };

    bool stateRestored = true;
    auto checkStateRestored = [ & ]() {
        GLboolean mask[ 4 ] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
        glGetBooleanv( GL_COLOR_WRITEMASK, mask );
        bool good = glIsEnabled( GL_SCISSOR_TEST ) && glIsEnabled( GL_CULL_FACE )
                 && glIsEnabled( GL_DEPTH_TEST ) && glIsEnabled( GL_STENCIL_TEST )
                 && mask[0] == GL_FALSE && mask[1] == GL_TRUE
                 && mask[2] == GL_FALSE && mask[3] == GL_TRUE;
        if( !good )
            stateRestored = false;
    };

    // A host owns its target and hands it over fresh. Without this the second
    // run would still be looking at the first run's pixels wherever the plugin
    // failed to write -- which is exactly the failure this mode hunts for.
    auto clearHost = [ & ]() {
        glDisable( GL_SCISSOR_TEST );
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glBindFramebuffer( GL_FRAMEBUFFER, hostFbo );
        const float transparent[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
        glClearBufferfv( GL_COLOR, 0, transparent );
    };

    int failedFrame = -1;
    auto runScene = [ & ]( bool hostile ) {
        clearHost();
        t = 0.0f;
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

            if( hostile )
                applyHostileState();
            ps.DrawFrame(tracker, params, dt, t, W, H, hostFbo);
            if( hostile )
            {
                checkStateRestored();
                clearHostileState();
            }

            GLenum err = glGetError();
            if( err != GL_NO_ERROR ) { printf("GL error 0x%x at frame %d\n", err, frame); failedFrame = frame; return; }

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
    };

    runScene( false );
    if( failedFrame >= 0 ) return 1;

    const int cleanLit       = countLit();
    const long long cleanRed = sumRed();

    int hostileLit = -1;
    long long hostileRed = -1;
    if( hostileTest )
    {
        // Same scene again from scratch, this time with the host fighting us.
        ps.DeInit();
        if( !ps.Init() ) { printf("re-init failed\n"); return 1; }
        ps.SetTextureSize( 128 );
        tracker.Reset();
        runScene( true );
        clearHostileState();
        if( failedFrame >= 0 ) return 1;
        hostileLit = countLit();
        hostileRed = sumRed();
    }

    readHost();
    for( size_t i=0;i<pixels.size();i+=4 ) if( pixels[i]||pixels[i+1]||pixels[i+2] ) ++nonZeroPixels;
    printf("lit pixels: %d / %d (%.1f%%)  bodies=%d\n", nonZeroPixels, W*H,
           100.0*nonZeroPixels/(W*H), tracker.ActiveBodies());

    // Write a PPM (flipped, since GL origin is bottom-left).
    const char* outName = "headless_out.ppm";
    if( twoBodies )       outName = "headless_two.ppm";
    else if( hostileTest ) outName = "headless_hostile.ppm";
    else if( handsTest )  outName = "headless_hands.ppm";
    else if( headTest )   outName = "headless_head.ppm";
    else if( argc > 1 )   outName = "headless_alt.ppm";
    FILE* f = fopen( outName, "wb" );
    fprintf(f,"P6\n%d %d\n255\n",W,H);
    for(int y=H-1;y>=0;--y) for(int x=0;x<W;++x){ const unsigned char* p=&pixels[(y*W+x)*4]; fwrite(p,1,3,f); }
    fclose(f);

    bool ok = nonZeroPixels > 200;
    printf("%s\n", ok ? "PASS: particles rendered" : "FAIL: frame is empty");

    if( handsTest || headTest )
    {
        // Where the light is. Hands: the figure's hands sit in the outer
        // quarters, so the middle half (torso, legs, head) should be nearly
        // dark. Head: the face is in the top fifth, so almost everything lit
        // should be up there. GL rows count from the bottom.
        long long inside = 0, lit = 0;
        for( int y = 0; y < H; ++y )
            for( int x = 0; x < W; ++x )
            {
                const unsigned char* p = &pixels[ ( y * W + x ) * 4 ];
                if( !( p[0] || p[1] || p[2] ) ) continue;
                ++lit;
                const float u = ( x + 0.5f ) / W, vTop = 1.0f - ( y + 0.5f ) / H;
                const bool in = handsTest ? ( u < 0.25f || u > 0.75f ) : ( vTop < 0.35f && u > 0.3f && u < 0.7f );
                if( in ) ++inside;
            }
        const double share = lit > 0 ? double( inside ) / double( lit ) : 0.0;
        printf( "%s: %.1f%% of lit pixels in the expected region\n", handsTest ? "hands" : "head", share * 100.0 );
        const bool placed = share > 0.9;
        printf( "%s\n", placed ? "PASS: particles stay on the selected part"
                                : "FAIL: particles are coming from elsewhere" );
        ok = ok && placed;
    }

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

    if( hostileTest )
    {
        printf("clean lit=%d red=%lld | hostile lit=%d red=%lld\n",
               cleanLit, cleanRed, hostileLit, hostileRed);
        // The two runs are the same scene, so they must match exactly: a
        // scissor box would cut the frame down, a colour mask would zero red,
        // culling would drop the fullscreen passes entirely.
        const bool identical = hostileLit == cleanLit && hostileRed == cleanRed;
        printf("%s\n", identical ? "PASS: host state did not affect our output"
                                  : "FAIL: hostile host state changed the render");
        printf("%s\n", stateRestored ? "PASS: host state was handed back untouched"
                                      : "FAIL: host state was not restored");
        ok = ok && identical && stateRestored;
    }

    ps.DeInit();
    return ok ? 0 : 1;
}
