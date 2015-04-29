#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/time.h>
#endif

#include "chowconfig.h"
#include "platform.h"
#include "include_gl.h"
#include "manager.h"
#include "mathcommon.h"
#include "fbo.h"
#include <iostream>
#include "platform.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <time.h>
#include <boost/cstdint.hpp>
#include "path.h"
#include "render.h"

enum ScaleType
{
    BEST_FIT = 0,
    BEST_FIT_INT = 1,
    EXACT_FIT = 2
};

static Framebuffer screen_fbo;
static SDL_Window * global_window = NULL;
static int global_window_id;

#ifdef CHOWDREN_USE_D3D
static D3DPRESENT_PARAMETERS pparams;
#else
static SDL_GLContext global_context = NULL;
#endif

static bool is_fullscreen = false;
static bool hide_cursor = false;
static bool has_closed = false;
static Uint64 start_time;
int scale_type = BEST_FIT;

static int draw_x_size = 0;
static int draw_y_size = 0;
static int draw_x_off = 0;
static int draw_y_off = 0;

#ifdef CHOWDREN_USE_GL
// opengl function pointers
PFNGLBLENDEQUATIONSEPARATEEXTPROC __glBlendEquationSeparateEXT;
PFNGLBLENDEQUATIONEXTPROC __glBlendEquationEXT;
PFNGLBLENDFUNCSEPARATEEXTPROC __glBlendFuncSeparateEXT;
PFNGLACTIVETEXTUREARBPROC __glActiveTextureARB;
PFNGLCLIENTACTIVETEXTUREARBPROC __glClientActiveTextureARB;
PFNGLGENFRAMEBUFFERSEXTPROC __glGenFramebuffersEXT;
PFNGLFRAMEBUFFERTEXTURE2DEXTPROC __glFramebufferTexture2DEXT;
PFNGLBINDFRAMEBUFFEREXTPROC __glBindFramebufferEXT;

PFNGLUSEPROGRAMOBJECTARBPROC __glUseProgramObjectARB;
PFNGLDETACHOBJECTARBPROC __glDetachObjectARB;
PFNGLGETINFOLOGARBPROC __glGetInfoLogARB;
PFNGLGETOBJECTPARAMETERIVARBPROC __glGetObjectParameterivARB;
PFNGLLINKPROGRAMARBPROC __glLinkProgramARB;
PFNGLCREATEPROGRAMOBJECTARBPROC __glCreateProgramObjectARB;
PFNGLATTACHOBJECTARBPROC __glAttachObjectARB;
PFNGLCOMPILESHADERARBPROC __glCompileShaderARB;
PFNGLSHADERSOURCEARBPROC __glShaderSourceARB;
PFNGLCREATESHADEROBJECTARBPROC __glCreateShaderObjectARB;
PFNGLUNIFORM1IARBPROC __glUniform1iARB;
PFNGLUNIFORM2FARBPROC __glUniform2fARB;
PFNGLUNIFORM1FARBPROC __glUniform1fARB;
PFNGLUNIFORM4FARBPROC __glUniform4fARB;
PFNGLGETUNIFORMLOCATIONARBPROC __glGetUniformLocationARB;
#endif

#ifndef CHOWDREN_USE_D3D
static bool check_opengl_extension(const char * name)
{
    if (SDL_GL_ExtensionSupported(name) == SDL_TRUE)
        return true;
    std::string message;
    message += "OpenGL extension '";
    message += name;
    message += "' not supported.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenGL error",
                             message.c_str(), NULL);
    return false;
}

const char * extensions[] = {
#ifdef CHOWDREN_USE_GL
    "GL_EXT_framebuffer_object",
    "GL_ARB_shader_objects",
    "GL_ARB_vertex_shader",
    "GL_ARB_fragment_shader",
    "GL_ARB_texture_non_power_of_two",
#endif
    NULL
};

static bool check_opengl_extensions()
{
    for (int i = 0; extensions[i] != NULL; i++)
        if (!check_opengl_extension(extensions[i]))
            return false;
    return true;
}
#endif

static void on_key(SDL_KeyboardEvent & e)
{
    if (e.repeat != 0)
        return;
    bool state = e.state == SDL_PRESSED;
    int key = e.keysym.sym;

    if (state && key == SDLK_RETURN && (SDL_GetModState() & KMOD_ALT)) {
        manager.set_window(!manager.fullscreen);
        return;
    }

    if (state && key == SDLK_F2) {
        manager.frame->restart();
    }

#ifdef CHOWDREN_USE_EDITOBJ
    if (state && key == SDLK_v && (SDL_GetModState() & KMOD_CTRL) &&
        SDL_HasClipboardText())
    {
        char * text = SDL_GetClipboardText();
        manager.input += text;
        SDL_free(text);
        return;
    }
#endif

    manager.on_key(key, state);
}

static void on_mouse(SDL_MouseButtonEvent & e)
{
    manager.on_mouse(e.button, e.state == SDL_PRESSED);
}

void init_joystick();
void update_joystick();
void add_joystick(int device);
void remove_joystick(int instance);
void on_joystick_button(int instance, int button, bool state);
void on_controller_button(int instance, int button, bool state);
void d3d_reset(int w, int h);
void reset_d3d_state();

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <sys/param.h> // For MAXPATHLEN

static void set_resources_dir()
{
    char resourcesPath[MAXPATHLEN];

    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle)
        return;

    CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(bundle);

    CFStringRef last = CFURLCopyLastPathComponent(resourcesURL);
    if (CFStringCompare(CFSTR("Resources"), last, 0) != kCFCompareEqualTo)
    {
        CFRelease(last);
        CFRelease(resourcesURL);
        return;
    }

    CFRelease(last);

    if (!CFURLGetFileSystemRepresentation(resourcesURL,
                                          true,
                                          (UInt8*) resourcesPath,
                                          MAXPATHLEN))
    {
        CFRelease(resourcesURL);
        return;
    }

    CFRelease(resourcesURL);

    chdir(resourcesPath);
}
#endif

#ifndef NDEBUG
static void sdl_log(void *userdata, int category, SDL_LogPriority priority,
                    const char * message)
{
    std::cout << "SDL log (" << category << ", " << priority << "): "
              << message << std::endl;
}
#endif

void platform_init()
{
    unsigned int flags = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER |
                         SDL_INIT_HAPTIC | SDL_INIT_NOPARACHUTE;
    if (SDL_Init(flags) < 0) {
        std::cout << "SDL could not be initialized: " << SDL_GetError()
            << std::endl;
        return;
    }
    SDL_EventState(SDL_MOUSEMOTION, SDL_DISABLE);
    // SDL_EventState(SDL_WINDOWEVENT, SDL_DISABLE);
#ifdef __APPLE__
    set_resources_dir();
#endif
    start_time = SDL_GetPerformanceCounter();
    init_joystick();

#ifdef _WIN32
    timeBeginPeriod(1);
#endif

#ifndef NDEBUG
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_WARN);
    SDL_LogSetOutputFunction(sdl_log, NULL);
#endif
}

// Here, we check if vsync is causing the game to slow down from 60fps to lower
// than ~55fps.

static int vsync_value = -2;
static double vsync_fail_time = 0.0;

void check_vsync()
{
    double expected = 1.0 / manager.fps_limit.framerate;
    if (manager.fps_limit.dt - expected > 0.0015) {
        vsync_fail_time += expected;
    } else {
        vsync_fail_time = 0.0;
        return;
    }

    if (vsync_fail_time > 2.0) {
        std::cout << "Swap interval causing slowdown, turning off vsync"
            << std::endl;
        vsync_fail_time = 0.0;
        platform_set_vsync(0);
    }
}

void platform_poll_events()
{
    if (vsync_value == 1)
        check_vsync();

#ifdef CHOWDREN_USE_EDITOBJ
    manager.input.clear();
#endif

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                on_key(e.key);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                on_mouse(e.button);
                break;
            case SDL_JOYDEVICEADDED:
                add_joystick(e.jdevice.which);
                break;
            case SDL_JOYDEVICEREMOVED:
                remove_joystick(e.jdevice.which);
                break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                on_joystick_button(e.jbutton.which, e.jbutton.button,
                                   e.jbutton.state == SDL_PRESSED);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                on_controller_button(e.cbutton.which, e.cbutton.button,
                                     e.cbutton.state == SDL_PRESSED);
                break;
#ifdef CHOWDREN_USE_EDITOBJ
            case SDL_TEXTINPUT:
                manager.input += e.text.text;
                break;
#endif
            case SDL_QUIT:
                has_closed = true;
                break;
#ifdef CHOWDREN_USE_D3D
            case SDL_WINDOWEVENT:
                if (e.window.windowID != global_window_id)
                    break;
                if (e.window.event != SDL_WINDOWEVENT_SIZE_CHANGED)
                    break;
                d3d_reset(e.window.data1, e.window.data2);
                break;
#endif
            default:
                break;
        }
    }
    update_joystick();
}

// time

double platform_get_time()
{
    Uint64 s = SDL_GetPerformanceCounter();
    s -= start_time;
    return s / double(SDL_GetPerformanceFrequency());
}

unsigned int platform_get_global_time()
{
    return time(NULL);
}

void platform_sleep(double t)
{
    if (t < 0.001)
        return;
    SDL_Delay(t * 1000.0);
}

bool platform_display_closed()
{
    if (global_window == NULL)
        return true;
    return has_closed;
}

void platform_get_mouse_pos(int * x, int * y)
{
    SDL_GetMouseState(x, y);
    *x = (*x - draw_x_off) * (float(WINDOW_WIDTH) / draw_x_size);
    *y = (*y - draw_y_off) * (float(WINDOW_HEIGHT) / draw_y_size);
}

// #define CHOWDREN_USE_GL_DEBUG

#if !defined(NDEBUG) && !defined(CHOWDREN_USE_D3D)
static void APIENTRY on_debug_message(GLenum source, GLenum type, GLuint id,
                                      GLenum severity, GLsizei length,
                                      const GLchar * message,
                                      GLvoid * param)
{
    std::cout << "OpenGL message (" << source << " " << type << " " << id <<
        ")" << message << std::endl;
}
#endif

#ifdef CHOWDREN_USE_D3D
static void create_d3d_device()
{
    int display_index = SDL_GetWindowDisplayIndex(global_window);
    int adapter_index = SDL_Direct3D9GetAdapterIndex(display_index);

    HRESULT hr;
    D3DDEVTYPE device_type = D3DDEVTYPE_HAL;
    DWORD device_flags = D3DCREATE_FPU_PRESERVE |
                         D3DCREATE_HARDWARE_VERTEXPROCESSING |
                         D3DCREATE_PUREDEVICE;

    hr = render_data.d3d->CreateDevice(adapter_index, device_type,
                                       pparams.hDeviceWindow, device_flags,
                                       &pparams, &render_data.device);
    if (!FAILED(hr))
        return;

    // try again with fixed back buffer count
    hr = render_data.d3d->CreateDevice(adapter_index, device_type,
                                       pparams.hDeviceWindow, device_flags,
                                       &pparams, &render_data.device);
    if (!FAILED(hr))
        return;

    // Try to create the device with mixed vertex processing.
    device_flags &= ~D3DCREATE_HARDWARE_VERTEXPROCESSING;
    device_flags &= ~D3DCREATE_PUREDEVICE;
    device_flags |= D3DCREATE_MIXED_VERTEXPROCESSING;

    hr = render_data.d3d->CreateDevice(adapter_index, device_type,
                                       pparams.hDeviceWindow, device_flags,
                                       &pparams, &render_data.device);
    if (!FAILED(hr))
        return;

    // try to create the device with software vertex processing.
    device_flags &= ~D3DCREATE_MIXED_VERTEXPROCESSING;
    device_flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    hr = render_data.d3d->CreateDevice(adapter_index, device_type,
                                       pparams.hDeviceWindow, device_flags,
                                       &pparams, &render_data.device);
    if (!FAILED(hr))
        return;

    // try reference device
    device_type = D3DDEVTYPE_REF;
    hr = render_data.d3d->CreateDevice(adapter_index, device_type,
                                       pparams.hDeviceWindow, device_flags,
                                       &pparams, &render_data.device);
    if (!FAILED(hr))
        return;

    std::cout << "Could not create D3D context" << std::endl;
    exit(EXIT_FAILURE);
}

void d3d_reset(int w, int h)
{
    pparams.BackBufferWidth = w;
    pparams.BackBufferHeight = h;

    for (int i = 0; i < 32; ++i) {
        Framebuffer * fbo = Framebuffer::fbos[i];
        if (fbo == NULL)
            continue;
        fbo->fbo->Release();
        TextureData & t = render_data.textures[fbo->tex];
        t.texture->Release();
    }

    render_data.default_target->Release();

    render_data.device->Reset(&pparams);

    for (int i = 0; i < 32; ++i) {
        Framebuffer * fbo = Framebuffer::fbos[i];
        if (fbo == NULL)
            continue;
        fbo->init(fbo->w, fbo->h);
    }

    render_data.device->GetRenderTarget(0, &render_data.default_target);

    reset_d3d_state();
}
#endif

void platform_create_display(bool fullscreen)
{
    is_fullscreen = fullscreen;

    int flags = SDL_WINDOW_RESIZABLE;
    if (fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

#ifndef CHOWDREN_USE_D3D
    flags |= SDL_WINDOW_OPENGL;

#ifdef CHOWDREN_USE_GL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#elif CHOWDREN_USE_GLES1
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#elif CHOWDREN_USE_GLES2
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

#ifndef NDEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

#endif // CHOWDREN_USE_D3D
    global_window = SDL_CreateWindow(NAME,
                                     SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED,
                                     WINDOW_WIDTH, WINDOW_HEIGHT,
                                     flags);
    global_window_id = SDL_GetWindowID(global_window);

    if (global_window == NULL) {
        std::cout << "Could not open window: " << SDL_GetError() << std::endl;
        exit(EXIT_FAILURE);
        return;
    }

#ifdef __linux
    SDL_Surface * icon = SDL_LoadBMP("icon.bmp");
    if (icon != NULL) {
        SDL_SetWindowIcon(global_window, icon);
        SDL_FreeSurface(icon);
    }
#endif

#ifdef CHOWDREN_USE_D3D
    render_data.d3d = Direct3DCreate9(D3D_SDK_VERSION);
    SDL_SysWMinfo window_info;
    SDL_VERSION(&window_info.version);
    SDL_GetWindowWMInfo(global_window, &window_info);
    pparams.hDeviceWindow = window_info.info.win.window;
    pparams.BackBufferWidth = WINDOW_WIDTH;
    pparams.BackBufferHeight = WINDOW_HEIGHT;
    pparams.BackBufferCount = 1;
    pparams.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pparams.Windowed = TRUE;
    pparams.BackBufferFormat = D3DFMT_UNKNOWN;
    pparams.FullScreen_RefreshRateInHz = 0;
    pparams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    create_d3d_device();

    D3DCAPS9 caps;
    render_data.device->GetDeviceCaps(&caps);

    if ((caps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0 &&
        (caps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) == 0)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "D3D error",
                                 "NPOT textures not supported", NULL);
        exit(EXIT_FAILURE);
    }

    render_data.device->GetRenderTarget(0, &render_data.default_target);
#else
    global_context = SDL_GL_CreateContext(global_window);
    if (global_context == NULL) {
        std::cout << "Could not create OpenGL context: " << SDL_GetError()
            << std::endl;
        exit(EXIT_FAILURE);
        return;
    }

    const GLubyte * renderer = glGetString(GL_RENDERER);
    const GLubyte * vendor = glGetString(GL_VENDOR);

    std::cout << "Renderer: " << renderer << " - " << vendor << " - "
        << std::endl;

#ifdef CHOWDREN_USE_GL
    // initialize OpenGL function pointers
    __glBlendEquationSeparateEXT =
        (PFNGLBLENDEQUATIONSEPARATEEXTPROC)
        SDL_GL_GetProcAddress("glBlendEquationSeparateEXT");
    __glBlendEquationEXT =
        (PFNGLBLENDEQUATIONEXTPROC)
        SDL_GL_GetProcAddress("glBlendEquationEXT");
    __glBlendFuncSeparateEXT =
        (PFNGLBLENDFUNCSEPARATEEXTPROC)
        SDL_GL_GetProcAddress("glBlendFuncSeparateEXT");

    __glActiveTextureARB =
        (PFNGLACTIVETEXTUREARBPROC)
        SDL_GL_GetProcAddress("glActiveTextureARB");
    __glClientActiveTextureARB =
        (PFNGLACTIVETEXTUREARBPROC)
        SDL_GL_GetProcAddress("glClientActiveTextureARB");

    __glGenFramebuffersEXT =
        (PFNGLGENFRAMEBUFFERSEXTPROC)
        SDL_GL_GetProcAddress("glGenFramebuffersEXT");
    __glFramebufferTexture2DEXT =
        (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
        SDL_GL_GetProcAddress("glFramebufferTexture2DEXT");
    __glBindFramebufferEXT =
        (PFNGLBINDFRAMEBUFFEREXTPROC)
        SDL_GL_GetProcAddress("glBindFramebufferEXT");

    // shaders
    __glUniform1iARB =
        (PFNGLUNIFORM1IARBPROC)
        SDL_GL_GetProcAddress("glUniform1iARB");
    __glUseProgramObjectARB =
        (PFNGLUSEPROGRAMOBJECTARBPROC)
        SDL_GL_GetProcAddress("glUseProgramObjectARB");
    __glDetachObjectARB =
        (PFNGLDETACHOBJECTARBPROC)
        SDL_GL_GetProcAddress("glDetachObjectARB");
    __glGetInfoLogARB =
        (PFNGLGETINFOLOGARBPROC)
        SDL_GL_GetProcAddress("glGetInfoLogARB");
    __glGetObjectParameterivARB =
        (PFNGLGETOBJECTPARAMETERIVARBPROC)
        SDL_GL_GetProcAddress("glGetObjectParameterivARB");
    __glLinkProgramARB =
        (PFNGLLINKPROGRAMARBPROC)
        SDL_GL_GetProcAddress("glLinkProgramARB");
    __glCreateProgramObjectARB =
        (PFNGLCREATEPROGRAMOBJECTARBPROC)
        SDL_GL_GetProcAddress("glCreateProgramObjectARB");
    __glAttachObjectARB =
        (PFNGLATTACHOBJECTARBPROC)
        SDL_GL_GetProcAddress("glAttachObjectARB");
    __glCompileShaderARB =
        (PFNGLCOMPILESHADERARBPROC)
        SDL_GL_GetProcAddress("glCompileShaderARB");
    __glShaderSourceARB =
        (PFNGLSHADERSOURCEARBPROC)
        SDL_GL_GetProcAddress("glShaderSourceARB");
    __glCreateShaderObjectARB =
        (PFNGLCREATESHADEROBJECTARBPROC)
        SDL_GL_GetProcAddress("glCreateShaderObjectARB");
    __glUniform2fARB =
        (PFNGLUNIFORM2FARBPROC)
        SDL_GL_GetProcAddress("glUniform2fARB");
    __glUniform1fARB =
        (PFNGLUNIFORM1FARBPROC)
        SDL_GL_GetProcAddress("glUniform1fARB");
    __glUniform4fARB =
        (PFNGLUNIFORM4FARBPROC)
        SDL_GL_GetProcAddress("glUniform4fARB");
    __glGetUniformLocationARB =
        (PFNGLGETUNIFORMLOCATIONARBPROC)
        SDL_GL_GetProcAddress("glGetUniformLocationARB");
#endif

#if !defined(NDEBUG) && defined(CHOWDREN_USE_GL_DEBUG)
#define GL_DEBUG_OUTPUT 0x92E0
	std::cout << "OpenGL debug enabled" << std::endl;
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);

    PFNGLDEBUGMESSAGECALLBACKARBPROC glDebugMessageCallback =
        (PFNGLDEBUGMESSAGECALLBACKARBPROC)
        SDL_GL_GetProcAddress("glDebugMessageCallback");

    PFNGLDEBUGMESSAGECONTROLARBPROC glDebugMessageControl =
        (PFNGLDEBUGMESSAGECONTROLARBPROC)
        SDL_GL_GetProcAddress("glDebugMessageControl");

	glDebugMessageCallback((GLDEBUGPROCARB)on_debug_message, NULL);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL,
                          GL_TRUE);
#endif

    // check extensions
    if (!check_opengl_extensions()) {
        std::cout << "Not all OpenGL extensions supported. Quitting..."
            << std::endl;
        exit(EXIT_FAILURE);
        return;
    }

#endif // CHOWDREN_USE_D3D

    // if the cursor was hidden before the window was created, hide it now
    if (hide_cursor)
        platform_hide_mouse();

    screen_fbo.init(WINDOW_WIDTH, WINDOW_HEIGHT);
}

void platform_set_vsync(bool value)
{
    if (global_window == NULL)
        return;

    int vsync;
    if (value)
        vsync = 1;
    else
        vsync = 0;

    if (vsync == vsync_value)
        return;
    vsync_value = vsync;

#ifdef CHOWDREN_USE_D3D
    if (vsync == 1)
        pparams.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    else
        pparams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3d_reset(pparams.BackBufferWidth, pparams.BackBufferHeight);
#else
    int ret = SDL_GL_SetSwapInterval(vsync);
    if (ret == 0)
        return;

    std::cout << "Set vsync failed: " << SDL_GetError() << std::endl;
#endif
}

bool platform_get_vsync()
{
#ifdef CHOWDREN_USE_D3D
    return pparams.PresentationInterval == D3DPRESENT_INTERVAL_ONE;
#else
    int vsync = SDL_GL_GetSwapInterval();
    if (vsync != -1)
        return vsync == 1;
    switch (vsync_value) {
        case 1:
        case -1:
            return true;
        default:
            return false;
    }
#endif
}

#define CHOWDREN_DESKTOP_FULLSCREEN

void platform_set_fullscreen(bool value)
{
    if (value == is_fullscreen)
        return;
    is_fullscreen = value;
#ifdef CHOWDREN_DESKTOP_FULLSCREEN
    int flags;
    if (value)
        flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    else
        flags = 0;
#else
    int flags;
    if (value) {
        flags = SDL_WINDOW_FULLSCREEN;
        int display = SDL_GetWindowDisplayIndex(global_window);
        SDL_DisplayMode mode;
        SDL_GetDesktopDisplayMode(display, &mode);
        SDL_SetWindowDisplayMode(global_window, &mode);
    } else
        flags = 0;
#endif
    SDL_SetWindowFullscreen(global_window, flags);
    if (value)
        return;
    SDL_SetWindowPosition(global_window,
                          SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
}

void platform_begin_draw()
{
    render_data.device->BeginScene();
    screen_fbo.bind();
}

void platform_swap_buffers()
{
    int window_width, window_height;
    platform_get_size(&window_width, &window_height);
    bool resize = window_width != WINDOW_WIDTH ||
                  window_height != WINDOW_HEIGHT;

    int use_scale_type = scale_type;
    if (!is_fullscreen)
        use_scale_type = BEST_FIT;

    if (resize && use_scale_type == EXACT_FIT) {
        draw_x_size = window_width;
        draw_y_size = window_height;
    } else if (resize) {
        // aspect-aware resize
        float aspect_width = window_width / float(WINDOW_WIDTH);
        float aspect_height = window_height / float(WINDOW_HEIGHT);

        float aspect = std::min(aspect_width, aspect_height);
        if (use_scale_type == BEST_FIT_INT)
            aspect = floor(aspect);

        draw_x_size = aspect * WINDOW_WIDTH;
        draw_y_size = aspect * WINDOW_HEIGHT;
    } else {
        draw_x_size = WINDOW_WIDTH;
        draw_y_size = WINDOW_HEIGHT;
    }

    draw_x_off = (window_width - draw_x_size) / 2;
    draw_y_off = (window_height - draw_y_size) / 2;

    screen_fbo.unbind();

    // resize the window contents if necessary (fullscreen mode)
    Render::set_view(0, 0, window_width, window_height);
    Render::set_offset(0, 0);
    Render::clear(0, 0, 0, 255);

    int x2 = draw_x_off + draw_x_size;
    int y2 = draw_y_off + draw_y_size;

#ifdef CHOWDREN_SPECIAL_POINT_FILTER
    float x_scale = draw_x_size / float(WINDOW_WIDTH);
    float y_scale = draw_y_size / float(WINDOW_WIDTH);
    float use_effect = true;
    if (use_effect) {        
        Render::set_effect(Render::PIXELSCALE);
        set_scale_uniform(WINDOW_WIDTH, WINDOW_HEIGHT,
                          draw_x_size / float(WINDOW_WIDTH),
                          draw_y_size / float(WINDOW_HEIGHT));
    }
#endif

    Render::disable_blend();
#ifdef CHOWDREN_USE_D3D
    Render::draw_tex(draw_x_off, draw_y_off, x2, y2, Color(),
                     screen_fbo.get_tex());
#else
	Render::draw_tex(draw_x_off, y2, x2, draw_y_off, Color(),
                     screen_fbo.get_tex());
#endif
    Render::enable_blend();

#ifdef CHOWDREN_SPECIAL_POINT_FILTER
    if (use_effect)
        Render::disable_effect();
#endif

#ifdef CHOWDREN_USE_D3D
    render_data.device->EndScene();
    render_data.device->Present(NULL, NULL, NULL, NULL);
#else
    SDL_GL_SwapWindow(global_window);
#endif
}

void platform_get_size(int * width, int * height)
{
#ifdef CHOWDREN_USE_D3D
    SDL_GetWindowSize(global_window, width, height);
#else
    SDL_GL_GetDrawableSize(global_window, width, height);
#endif
}

void platform_get_screen_size(int * width, int * height)
{
    int display_index;
    display_index = SDL_GetWindowDisplayIndex(global_window);
    SDL_Rect bounds;
    SDL_GetDisplayBounds(display_index, &bounds);
    *width = bounds.w;
    *height = bounds.h;
}

void platform_set_display_scale(int scale)
{
    SDL_SetWindowSize(global_window,
                      WINDOW_WIDTH * scale,
                      WINDOW_HEIGHT * scale);
    SDL_SetWindowPosition(global_window,
                          SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
}

void platform_set_scale_type(int type)
{
    scale_type = type;
}

bool platform_has_focus()
{
    int f = SDL_GetWindowFlags(global_window);
    if ((f & SDL_WINDOW_SHOWN) == 0)
        return false;
    if ((f & SDL_WINDOW_INPUT_FOCUS) == 0)
        return false;
    return true;
}

void platform_set_focus(bool value)
{
    if (value)
        SDL_RestoreWindow(global_window);
    else
        SDL_MinimizeWindow(global_window);
}

void platform_show_mouse()
{
    hide_cursor = false;
    SDL_ShowCursor(1);
}

void platform_hide_mouse()
{
    hide_cursor = true;
    SDL_ShowCursor(0);
}

const std::string & platform_get_language()
{
    static std::string language("English");
    return language;
}

// filesystem stuff

#include <sys/stat.h>

size_t platform_get_file_size(const char * filename)
{
#ifndef _WIN32
    struct stat path_stat;
    if (stat(filename, &path_stat) != 0)
        return 0;
    if (!S_ISREG(path_stat.st_mode))
        return 0;
    return path_stat.st_size;
#else
    WIN32_FILE_ATTRIBUTE_DATA fad;

    if (GetFileAttributesExA(filename, GetFileExInfoStandard, &fad) == 0)
        return 0;

    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return 0;

    size_t size = (uint64_t(fad.nFileSizeHigh) << (sizeof(fad.nFileSizeLow)*8))
                  + fad.nFileSizeLow;
    return size;
#endif
}

enum FileType
{
    NoneFile,
    RegularFile,
    Directory
};

static FileType get_file_type(const char * filename)
{
#ifndef _WIN32
    struct stat path_stat;
    if (stat(filename, &path_stat) != 0)
        return NoneFile;
    if (S_ISDIR(path_stat.st_mode))
        return Directory;
    if (S_ISREG(path_stat.st_mode))
        return RegularFile;
    return NoneFile;
#else

    DWORD attr(GetFileAttributesA(filename));
    if (attr == 0xFFFFFFFF)
        return NoneFile;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return Directory;
    else
        return RegularFile;
#endif
}

bool platform_path_exists(const std::string & value)
{
    return get_file_type(value.c_str()) != NoneFile;
}

bool platform_is_directory(const std::string & value)
{
    return get_file_type(value.c_str()) == Directory;
}

bool platform_is_file(const std::string & value)
{
    return get_file_type(value.c_str()) == RegularFile;
}

static bool create_dirs(const std::string & directory)
{
    if (directory.empty())
        return true;

    if (get_file_type(directory.c_str()) != NoneFile)
        return true;

    size_t slash = directory.find_last_of(PATH_SEP);
    if (slash != std::string::npos) {
        if (!create_dirs(directory.substr(0, slash)))
            return false;
    }

#ifdef _WIN32
    BOOL result = CreateDirectoryA(directory.c_str(), NULL);
    if (CreateDirectoryA(directory.c_str(), NULL) == FALSE)
        return false;
#else
    if (mkdir(directory.c_str(), S_IRWXU|S_IRWXG|S_IRWXO) != 0)
        return false;
#endif
    return true;
}

void platform_create_directories(const std::string & value)
{
    create_dirs(value);
}

#ifdef _WIN32
#include "windows.h"
#include "shlobj.h"
#elif __APPLE__
#include <CoreServices/CoreServices.h>
#include <limits.h>
#elif __linux
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

const std::string & platform_get_appdata_dir()
{
    static bool initialized = false;
    static std::string dir;
    if (!initialized) {
#ifdef _WIN32
        TCHAR path[MAX_PATH];
        SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, path);
        dir = path;
#elif __APPLE__
        FSRef ref;
        OSType folderType = kApplicationSupportFolderType;
        char path[PATH_MAX];
        FSFindFolder(kUserDomain, folderType, kCreateFolder, &ref);
        FSRefMakePath(&ref, (UInt8*)&path, PATH_MAX);
        dir = path;
#elif __linux
        struct passwd *pw = getpwuid(getuid());
        dir = pw->pw_dir;
#endif
    }
    return dir;
}

// joystick

static SDL_HapticEffect rumble_effect;

class JoystickData
{
public:
    SDL_Joystick * joy;
    SDL_GameController * controller;
    int instance;
    SDL_Haptic * haptics;
    bool has_effect, has_rumble;
    int axis_count;
    int last_press;
    int button_count;

    JoystickData()
    : has_effect(false), has_rumble(false), last_press(0), controller(NULL),
      haptics(NULL)
    {
    }

    void init(SDL_GameController * c, SDL_Joystick * j, int i)
    {
        controller = c;
        joy = j;
        instance = i;
        if (c == NULL)
            button_count = SDL_JoystickNumButtons(j);
        init_rumble();
    }

    void close()
    {
        if (controller != NULL)
            SDL_GameControllerClose(controller);
        if (haptics != NULL)
            SDL_HapticClose(haptics);
        controller = NULL;
        haptics = NULL;
    }

    void init_rumble()
    {
        if (SDL_JoystickIsHaptic(joy) != 1)
            return;
        haptics = SDL_HapticOpenFromJoystick(joy);
        if (haptics == NULL)
            return;
        int efx = SDL_HapticEffectSupported(haptics, &rumble_effect);
        if (efx == 1) {
            rumble_effect.leftright.large_magnitude = 0;
            rumble_effect.leftright.small_magnitude = 0;
            SDL_HapticNewEffect(haptics, &rumble_effect);
            has_effect = true;
        } else if (SDL_HapticRumbleSupported(haptics) == 1) {
            SDL_HapticRumbleInit(haptics);
            has_rumble = true;
        }
    }

    float get_axis(int n)
    {
        if (controller == NULL)
            return SDL_JoystickGetAxis(joy, n) / float(0x7FFF);
        if (n <= SDL_CONTROLLER_AXIS_INVALID || n >= SDL_CONTROLLER_AXIS_MAX)
            return 0.0f;
        return SDL_GameControllerGetAxis(controller,
                                         (SDL_GameControllerAxis)n)
               / float(0x7FFF);
    }

    bool get_button(int b)
    {
        if (controller == NULL)
            return get_button((SDL_GameControllerButton)b);
        if (b <= SDL_CONTROLLER_BUTTON_INVALID ||
            b >= SDL_CONTROLLER_BUTTON_MAX)
            return false;
        return get_button((SDL_GameControllerButton)b);
    }

    bool get_joy_button(int n)
    {
        return SDL_JoystickGetButton(joy, n) == 1;
    }

    bool get_button(SDL_GameControllerButton b)
    {
        if (controller == NULL) {
            int bb;
            switch (b) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    return (SDL_JoystickGetHat(joy, 0) & SDL_HAT_UP) != 0;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    return (SDL_JoystickGetHat(joy, 0) & SDL_HAT_DOWN) != 0;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    return (SDL_JoystickGetHat(joy, 0) & SDL_HAT_LEFT) != 0;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    return (SDL_JoystickGetHat(joy, 0) & SDL_HAT_RIGHT) != 0;
                default:
                    bb = (int)b;
            }
            if (bb >= SDL_CONTROLLER_BUTTON_DPAD_UP)
                bb -= 4;
            return SDL_JoystickGetButton(joy, bb) == 1;
        }
        return SDL_GameControllerGetButton(controller, b) == 1;
    }

    void vibrate(float left, float right, int ms)
    {
        if (!has_rumble && !has_effect)
            return;

        if (has_effect) {
            int lm = (int)(65535.0f * left);
            int sm = (int)(65535.0f * right);
            rumble_effect.leftright.large_magnitude = lm;
            rumble_effect.leftright.small_magnitude = sm;
            rumble_effect.leftright.length = ms;
            SDL_HapticUpdateEffect(haptics, 0, &rumble_effect);
            SDL_HapticRunEffect(haptics, 0, 1);
        } else {
            float strength;
            if (left >= right) {
                strength = left;
            } else {
                strength = right;
            }
            SDL_HapticRumblePlay(haptics, strength, ms);
        }
    }
};

static int selected_joy_index = 0;
static JoystickData * selected_joy = NULL;
static vector<JoystickData> joysticks;

#define CHOWDREN_SINGLE_JOYSTICK

JoystickData & get_joy(int n)
{
#ifdef CHOWDREN_SINGLE_JOYSTICK
    return *selected_joy;
#else
    return joysticks[n-1];
#endif
}

JoystickData * get_joy_instance(int instance)
{
    vector<JoystickData>::iterator it;
    for (it = joysticks.begin(); it != joysticks.end(); ++it) {
        JoystickData & j = *it;
        if (j.instance != instance)
            continue;
        return &j;
    }
    return NULL;
}

void add_joystick(int device)
{
    SDL_GameController * c = NULL;
    if (SDL_IsGameController(device))
        c = SDL_GameControllerOpen(device);

    SDL_Joystick * joy = NULL;
    if (c == NULL)
        joy = SDL_JoystickOpen(device);
    else
        joy = SDL_GameControllerGetJoystick(c);
    vector<JoystickData>::iterator it;
    for (it = joysticks.begin(); it != joysticks.end(); ++it) {
        JoystickData & j = *it;
        if (j.joy == joy) {
            if (c != NULL)
                SDL_GameControllerClose(c);
            else
                SDL_JoystickClose(joy);
            return;
        }
    }
    int instance = SDL_JoystickInstanceID(joy);
    int index = joysticks.size();
    joysticks.resize(index+1);
    joysticks[index].init(c, joy, instance);
    selected_joy = &joysticks[selected_joy_index];
}

void remove_joystick(int instance)
{
    if (selected_joy != NULL && selected_joy->instance == instance) {
        selected_joy = NULL;
        selected_joy_index = 0;
    }

    vector<JoystickData>::iterator it;
    for (it = joysticks.begin(); it != joysticks.end(); ++it) {
        JoystickData & j = *it;
        if (j.instance != instance)
            continue;
        j.close();
        joysticks.erase(it);
        return;
    }
}

void init_joystick()
{
    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");

    rumble_effect.type = SDL_HAPTIC_LEFTRIGHT;
    rumble_effect.leftright.length = 0;

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        add_joystick(i);
    }
}

void update_joystick()
{
}

void on_joystick_button(int instance, int button, bool state)
{
    JoystickData * joy = NULL;
    unsigned int i;
    for (i = 0; i < joysticks.size(); ++i) {
        JoystickData & j = joysticks[i];
        if (j.instance != instance)
            continue;
        joy = &j;
        break;
    }

    selected_joy_index = int(i);
    selected_joy = joy;
    if (!state)
        return;
    if (joy->controller != NULL)
        return;
    if (button >= SDL_CONTROLLER_BUTTON_DPAD_UP)
        button += 4;
    joy->last_press = button;
}

void on_controller_button(int instance, int button, bool state)
{
    JoystickData * joy = get_joy_instance(instance);
    joy->last_press = button;
}

int get_joystick_last_press(int n)
{
    if (!is_joystick_attached(n))
        return CHOWDREN_BUTTON_INVALID;
    return remap_button(get_joy(n).last_press + 1);
}

extern std::string empty_string;

const std::string & get_joystick_name(int n)
{
    if (!is_joystick_attached(n))
        return empty_string;
    static std::string ret;
    JoystickData & joy = get_joy(n);
    if (joy.controller == NULL)
        ret = SDL_JoystickName(joy.joy);
    else
        ret = SDL_GameControllerName(joy.controller);
    return ret;
}

bool is_joystick_attached(int n)
{
    n--;
#ifdef CHOWDREN_SINGLE_JOYSTICK
    if (n != 0)
        return false;
    return selected_joy != NULL;
#else
    return n >= 0 && n < int(joysticks.size());
#endif
}

bool is_joystick_pressed(int n, int button)
{
    if (!is_joystick_attached(n))
        return false;
    button = remap_button(button);
    button--;
    return get_joy(n).get_button(button);
}

bool any_joystick_pressed(int n)
{
    if (!is_joystick_attached(n))
        return false;
    JoystickData & joy = get_joy(n);
    if (joy.controller == NULL) {
        for (int i = 0; i < joy.button_count; i++) {
            if (joy.get_joy_button(i)) {
                return true;
            }
        }
        return false;
    }
    for (unsigned int i = 0; i < SDL_CONTROLLER_BUTTON_DPAD_UP; i++) {
        if (joy.get_button(i)) {
            return true;
        }
    }
    return false;
}

bool is_joystick_released(int n, int button)
{
    if (!is_joystick_attached(n))
        return true;
    button = remap_button(button);
    button--;
    return !get_joy(n).get_button(button);
}

void joystick_vibrate(int n, int l, int r, int ms)
{
    if (!is_joystick_attached(n))
        return;
    get_joy(n).vibrate(l / 100.0f, r / 100.0f, ms);
}

#define DEADZONE 0.15f
#define DEADZONE_MUL (1.0f / (1.0f - 0.15f))

float get_joystick_axis(int n, int axis)
{
    if (!is_joystick_attached(n))
        return 0.0f;
    axis--;
    float v = get_joy(n).get_axis(axis);
    if (v > DEADZONE)
        return (v - DEADZONE) * DEADZONE_MUL;
    else if (v < -DEADZONE)
        return (v + DEADZONE) * DEADZONE_MUL;
    else
        return 0.0f;
}

// url open

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>

void open_url(const std::string & name)
{
    ShellExecute(NULL, "open", name.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

#elif __APPLE__

void open_url(const std::string & name)
{
    CFStringRef str = CFStringCreateWithCString(0, name.c_str(), 0);
    CFURLRef ref = CFURLCreateWithString(0, str, 0);
    LSOpenCFURLRef(ref, 0);
    CFRelease(ref);
    CFRelease(str);
}

#elif __linux

#include <sys/types.h>
#include <dirent.h>

void open_url(const std::string & name)
{
    std::string cmd("xdg-open '");
    cmd += name;
    cmd += "'";
    system(cmd.c_str());
}

#endif

// file

#ifdef CHOWDREN_AUTO_STEAMCLOUD
#include "steam/steam_api.h"
#include "steam/steamtypes.h"
#include <sstream>
#include "../path.h"
#endif

bool platform_remove_file(const std::string & file)
{
#ifdef CHOWDREN_AUTO_STEAMCLOUD
    std::string base = get_path_filename(file);
    const char * base_c = base.c_str();
    if (SteamRemoteStorage()->FileExists(base_c)) {
        return SteamRemoteStorage()->FileDelete(base_c);
    }
#endif
    return remove(convert_path(file).c_str()) == 0;
}

#include "fileio.cpp"
#include "stdiofile.cpp"

// path

std::string convert_path(const std::string & v)
{
    std::string value = v;
    if (value.compare(0, 3, "./\\") == 0)
        value = std::string("./", 2) + value.substr(3);
#ifndef _WIN32
    std::replace(value.begin(), value.end(), '\\', '/');
#else
    std::replace(value.begin(), value.end(), '/', '\\');
#endif
    return value;
}

// debug

void platform_print_stats()
{

}



// dummies

void platform_prepare_frame_change()
{
}

void platform_set_remote_setting(const std::string & v)
{

}

void platform_set_remote_value(int v)
{

}

int platform_get_remote_value()
{
    return CHOWDREN_TV_TARGET;
}

void platform_set_border(bool v)
{

}

static std::string remote_string("TV");

const std::string & platform_get_remote_setting()
{
    return remote_string;
}

bool platform_has_error()
{
    return false;
}

void platform_debug(const std::string & value)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Debug",
                             value.c_str(), NULL);
}

void platform_exit()
{
#ifdef _WIN32
    timeEndPeriod(1);
#endif

    joysticks.clear();

    SDL_Quit();
}
