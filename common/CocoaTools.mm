// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#if ! __has_feature(objc_arc)
	#error "Compile this with -fobjc-arc"
#endif

#include "CocoaTools.h"
#include "Console.h"
#include "HostSys.h"
#include "WindowInfo.h"
#include <dlfcn.h>
#include <mutex>
#include <vector>
#include <Cocoa/Cocoa.h>
#include <QuartzCore/QuartzCore.h>

// MARK: - Metal Layers

static NSString*_Nonnull NSStringFromStringView(std::string_view sv)
{
	return [[NSString alloc] initWithBytes:sv.data() length:sv.size() encoding:NSUTF8StringEncoding];
}

bool CocoaTools::CreateMetalLayer(WindowInfo* wi)
{
	if (![NSThread isMainThread])
	{
		bool ret;
		dispatch_sync(dispatch_get_main_queue(), [&ret, wi]{ ret = CreateMetalLayer(wi); });
		return ret;
	}

	CAMetalLayer* layer = [CAMetalLayer layer];
	if (!layer)
	{
		Console.Error("Failed to create Metal layer.");
		return false;
	}

	NSView* view = (__bridge NSView*)wi->window_handle;
	[view setWantsLayer:YES];
	[view setLayer:layer];
	[layer setContentsScale:[[[view window] screen] backingScaleFactor]];
	// Store the layer pointer, that way MoltenVK doesn't call [NSView layer] outside the main thread.
	wi->surface_handle = (__bridge_retained void*)layer;
	return true;
}

void CocoaTools::DestroyMetalLayer(WindowInfo* wi)
{
	if (![NSThread isMainThread])
	{
		dispatch_sync_f(dispatch_get_main_queue(), wi, [](void* ctx){ DestroyMetalLayer(static_cast<WindowInfo*>(ctx)); });
		return;
	}

	NSView* view = (__bridge NSView*)wi->window_handle;
	CAMetalLayer* layer = (__bridge_transfer CAMetalLayer*)wi->surface_handle;
	if (!layer)
		return;
	wi->surface_handle = nullptr;
	[view setLayer:nil];
	[view setWantsLayer:NO];
}

std::optional<float> CocoaTools::GetViewRefreshRate(const WindowInfo& wi)
{
	if (![NSThread isMainThread])
	{
		std::optional<float> ret;
		dispatch_sync(dispatch_get_main_queue(), [&ret, wi]{ ret = GetViewRefreshRate(wi); });
		return ret;
	}

	std::optional<float> ret;
	NSView* const view = (__bridge NSView*)wi.window_handle;
	const u32 did = [[[[[view window] screen] deviceDescription] valueForKey:@"NSScreenNumber"] unsignedIntValue];
	if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(did))
	{
		ret = CGDisplayModeGetRefreshRate(mode);
		CGDisplayModeRelease(mode);
	}
	
	return ret;
}

// MARK: - Theme Change Handlers

@interface PCSX2KVOHelper : NSObject

- (void)addCallback:(void*)ctx run:(void(*)(void*))callback;
- (void)removeCallback:(void*)ctx;

@end

@implementation PCSX2KVOHelper
{
	std::vector<std::pair<void*, void(*)(void*)>> _callbacks;
}

- (void)addCallback:(void*)ctx run:(void(*)(void*))callback
{
	_callbacks.push_back(std::make_pair(ctx, callback));
}

- (void)removeCallback:(void*)ctx
{
	auto new_end = std::remove_if(_callbacks.begin(), _callbacks.end(), [ctx](const auto& entry){
		return ctx == entry.first;
	});
	_callbacks.erase(new_end, _callbacks.end());
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context
{
	for (const auto& callback : _callbacks)
		callback.second(callback.first);
}

@end

static PCSX2KVOHelper* s_themeChangeHandler;

void CocoaTools::AddThemeChangeHandler(void* ctx, void(handler)(void* ctx))
{
	assert([NSThread isMainThread]);
	if (!s_themeChangeHandler)
	{
		s_themeChangeHandler = [[PCSX2KVOHelper alloc] init];
		NSApplication* app = [NSApplication sharedApplication];
		[app addObserver:s_themeChangeHandler
		      forKeyPath:@"effectiveAppearance"
		         options:0
		         context:nil];
	}
	[s_themeChangeHandler addCallback:ctx run:handler];
}

void CocoaTools::RemoveThemeChangeHandler(void* ctx)
{
	assert([NSThread isMainThread]);
	[s_themeChangeHandler removeCallback:ctx];
}

void CocoaTools::MarkHelpMenu(void* menu)
{
	[NSApp setHelpMenu:(__bridge NSMenu*)menu];
}

// MARK: - Sound playback

bool Common::PlaySoundAsync(const char* path)
{
	NSString* nspath = [[NSString alloc] initWithUTF8String:path];
	NSSound* sound = [[NSSound alloc] initWithContentsOfFile:nspath byReference:YES];
	return [sound play];
}

// MARK: - Updater

std::optional<std::string> CocoaTools::GetBundlePath()
{
  std::optional<std::string> ret;
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[[NSBundle mainBundle] bundlePath]];
    if (url)
      ret = std::string([url fileSystemRepresentation]);
  }
  return ret;
}

std::optional<std::string> CocoaTools::GetNonTranslocatedBundlePath()
{
	// See https://objective-see.com/blog/blog_0x15.html

	NSURL* url = [NSURL fileURLWithPath:[[NSBundle mainBundle] bundlePath]];
	if (!url)
		return std::nullopt;

	if (void* handle = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY))
	{
		auto IsTranslocatedURL = reinterpret_cast<Boolean(*)(CFURLRef path, bool* isTranslocated, CFErrorRef*__nullable error)>(dlsym(handle, "SecTranslocateIsTranslocatedURL"));
		auto CreateOriginalPathForURL = reinterpret_cast<CFURLRef __nullable(*)(CFURLRef translocatedPath, CFErrorRef*__nullable error)>(dlsym(handle, "SecTranslocateCreateOriginalPathForURL"));
		bool is_translocated = false;
		if (IsTranslocatedURL)
			IsTranslocatedURL((__bridge CFURLRef)url, &is_translocated, nullptr);
		if (is_translocated)
		{
			if (CFURLRef actual = CreateOriginalPathForURL((__bridge CFURLRef)url, nullptr))
				url = (__bridge_transfer NSURL*)actual;
		}
		dlclose(handle);
	}

	return std::string([url fileSystemRepresentation]);
}

std::optional<std::string> CocoaTools::MoveToTrash(std::string_view file)
{
	NSURL* url = [NSURL fileURLWithPath:NSStringFromStringView(file)];
	NSURL* new_url;
	if (![[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:&new_url error:nil])
		return std::nullopt;
	return std::string([new_url fileSystemRepresentation]);
}

bool CocoaTools::DelayedLaunch(std::string_view file)
{
	@autoreleasepool {
		NSTask* task = [NSTask new];
		[task setExecutableURL:[NSURL fileURLWithPath:@"/bin/sh"]];
		[task setEnvironment:@{
			@"WAITPID": [NSString stringWithFormat:@"%d", [[NSProcessInfo processInfo] processIdentifier]],
			@"LAUNCHAPP": NSStringFromStringView(file),
		}];
		[task setArguments:@[@"-c", @"while /bin/ps -p $WAITPID > /dev/null; do /bin/sleep 0.1; done; exec /usr/bin/open \"$LAUNCHAPP\";"]];
		return [task launchAndReturnError:nil];
	}
}

// MARK: - Directory Services

bool CocoaTools::ShowInFinder(std::string_view file)
{
	return [[NSWorkspace sharedWorkspace] selectFile:NSStringFromStringView(file)
	                        inFileViewerRootedAtPath:@""];
}

std::optional<std::string> CocoaTools::GetResourcePath()
{ @autoreleasepool {
	if (NSBundle* bundle = [NSBundle mainBundle])
	{
		NSString* rsrc = [bundle resourcePath];
		NSString* root = [bundle bundlePath];
		if ([rsrc isEqualToString:root])
			rsrc = [rsrc stringByAppendingString:@"/resources"];
		return [rsrc UTF8String];
	}
	return std::nullopt;
}}

// MARK: - GSRunner

void* CocoaTools::CreateWindow(std::string_view title, u32 width, u32 height)
{
	if (!NSApp)
	{
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		[NSApp finishLaunching];
	}
	constexpr NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
	NSScreen* mainScreen = [NSScreen mainScreen];
	// Center the window on the screen, because why not
	NSRect screenFrame = [mainScreen frame];
	NSRect viewFrame = screenFrame;
	viewFrame.size = NSMakeSize(width, height);
	viewFrame.origin.x += (screenFrame.size.width - viewFrame.size.width) / 2;
	viewFrame.origin.y += (screenFrame.size.height - viewFrame.size.height) / 2;
	NSWindow* window = [[NSWindow alloc]
		initWithContentRect:viewFrame
		          styleMask:style
		            backing:NSBackingStoreBuffered
		              defer:NO];
	[window setTitle:NSStringFromStringView(title)];
	[window makeKeyAndOrderFront:window];
	return (__bridge_retained void*)window;
}

void CocoaTools::DestroyWindow(void* window)
{
	(void)(__bridge_transfer NSWindow*)window;
}

void CocoaTools::GetWindowInfoFromWindow(WindowInfo* wi, void* cf_window)
{
	if (cf_window)
	{
		NSWindow* window = (__bridge NSWindow*)cf_window;
		float scale = [window backingScaleFactor];
		NSView* view = [window contentView];
		NSRect dims = [view frame];
		wi->type = WindowInfo::Type::MacOS;
		wi->window_handle = (__bridge void*)view;
		wi->surface_width = dims.size.width * scale;
		wi->surface_height = dims.size.height * scale;
		wi->surface_scale = scale;
	}
	else
	{
		wi->type = WindowInfo::Type::Surfaceless;
	}
}

static constexpr short STOP_EVENT_LOOP = 0x100;

void CocoaTools::RunCocoaEventLoop(bool forever)
{
	NSDate* end = forever ? [NSDate distantFuture] : [NSDate distantPast];
	[NSApplication sharedApplication]; // Ensure NSApp is initialized
	while (true)
	{ @autoreleasepool {
		NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
		                                 untilDate:end
		                                    inMode:NSDefaultRunLoopMode
		                                   dequeue:YES];
		if (!ev || ([ev type] == NSEventTypeApplicationDefined && [ev subtype] == STOP_EVENT_LOOP))
			break;
		[NSApp sendEvent:ev];
	}}
}

void CocoaTools::StopMainThreadEventLoop()
{ @autoreleasepool {
	NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
	                                 location:{}
	                            modifierFlags:0
	                                timestamp:0
	                             windowNumber:0
	                                  context:nil
	                                  subtype:STOP_EVENT_LOOP
	                                    data1:0
	                                    data2:0];
	[NSApp postEvent:ev atStart:NO];
}}
