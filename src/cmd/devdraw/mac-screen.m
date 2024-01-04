#define Cursor OSXCursor
#define Point OSXPoint
#define Rect OSXRect

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#undef Cursor
#undef Point
#undef Rect

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <memlayer.h>
#include <mouse.h>
#include <cursor.h>
#include <keyboard.h>
#include <drawfcall.h>
#include "devdraw.h"
#include "bigarrow.h"
#include "glendapng.h"

AUTOFRAMEWORK(Cocoa)
AUTOFRAMEWORK(Metal)
AUTOFRAMEWORK(QuartzCore)
AUTOFRAMEWORK(CoreFoundation)

#define LOG	if(!0);else NSLog

/*
 * TODO: Maintain a list of views for the dock menu.
 */

static uint	keycvt(uint);
static int	mousebuttons(void);
static uint	msec(void);
static void	setprocname(char*);

static void	rpc_bouncemouse(Client*, Mouse);
static void	rpc_flush(Client*, Rectangle);
static void	rpc_resizeimg(Client*);
static void	rpc_resizewindow(Client*, Rectangle);
static void	rpc_setcursor(Client*, Cursor*, Cursor2*);
static void	rpc_setlabel(Client*, char*);
static void	rpc_setmouse(Client*, Point);
static void	rpc_topwin(Client*);

static ClientImpl macimpl =
{
	rpc_resizeimg,
	rpc_resizewindow,
	rpc_setcursor,
	rpc_setlabel,
	rpc_setmouse,
	rpc_topwin,
	rpc_bouncemouse,
	rpc_flush
};

@class DrawView;
@class DrawLayer;

@interface AppDelegate : NSObject<NSApplicationDelegate>
@end

static AppDelegate *myApp;

void
gfx_main(void)
{
	char *name;

	/*
	 * Set the process name, which is displayed in the application menu amongst
	 * other things, to the capitalized basename of the executable's filepath.
	 */
	if(client0!=nil && argv0!=nil){
		name = strrchr(argv0, '/');
		name = strdup(name!=nil ? name+1 : argv0);
		if(name != nil){
			name[0] = toupper((uchar)name[0]);
			setprocname(name);
			free(name);
		}
	}

	@autoreleasepool{
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		myApp = [AppDelegate new];
		[NSApp setDelegate:myApp];
		[NSApp run];
	}
}

void
rpc_shutdown(void)
{
	[NSApp terminate:myApp];
}

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(id)arg
{
	NSMenu *m, *sm;
	NSData *d;
	NSImage *i;

	LOG(@"applicationDidFinishLaunching");

	sm = [NSMenu new];
	[sm addItemWithTitle:@"Toggle Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
	[sm addItemWithTitle:@"Hide" action:@selector(hide:) keyEquivalent:@"h"];
	[sm addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
	m = [NSMenu new];
	[m addItemWithTitle:@"DEVDRAW" action:nil keyEquivalent:@""];
	[m setSubmenu:sm forItem:[m itemWithTitle:@"DEVDRAW"]];
	[NSApp setMainMenu:m];

	d = [[NSData alloc] initWithBytes:glenda_png length:(sizeof glenda_png)];
	i = [[NSImage alloc] initWithData:d];
	[NSApp setApplicationIconImage:i];
	[NSApp.dockTile display];

	gfx_started();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app
{
	return client0 != nil;
}
@end

@interface DrawLayer : CAMetalLayer
@property (nonatomic, retain) id<MTLCommandQueue> cmd;
@property (nonatomic, retain) id<MTLTexture> texture;
@end

@implementation DrawLayer
- (void)display
{
	LOG(@"display");
	LOG(@"display query drawable");

	@autoreleasepool{
		id<CAMetalDrawable> drawable;
		id<MTLCommandBuffer> cbuf;
		id<MTLBlitCommandEncoder> blit;

		drawable = self.nextDrawable;
		if(drawable == nil){
			LOG(@"display couldn't get drawable");
			[self setNeedsDisplay];
			return;
		}

		LOG(@"display got drawable");

		cbuf = [self.cmd commandBuffer];
		blit = [cbuf blitCommandEncoder];
		[blit copyFromTexture:self.texture
			sourceSlice:0
			sourceLevel:0
			sourceOrigin:MTLOriginMake(0, 0, 0)
			sourceSize:MTLSizeMake(self.texture.width, self.texture.height, self.texture.depth)
			toTexture:drawable.texture
			destinationSlice:0
			destinationLevel:0
			destinationOrigin:MTLOriginMake(0, 0, 0)];
		[blit endEncoding];

		[cbuf presentDrawable:drawable];
		drawable = nil;
		[cbuf addCompletedHandler:^(id<MTLCommandBuffer> b){
			if(b.error != nil)
				NSLog(@"command buffer finished with error: %@",
					b.error.localizedDescription);
			else
				LOG(@"command buffer finishes present drawable");
		}];
		[cbuf commit];
	}
	LOG(@"display commit");
}
@end

@interface DrawView : NSView<NSTextInputClient,NSWindowDelegate>
@property (nonatomic, assign) Client *client;
@property (nonatomic, retain) DrawLayer *dlayer;
@property (nonatomic, retain) NSCursor *currentCursor;
@property (nonatomic, assign) Memimage *img;

- (id)attach:(Client*)client winsize:(char*)winsize label:(char*)label;
- (void)topwin;
- (void)setlabel:(char*)label;
- (void)setcursor:(Cursor*)c cursor2:(Cursor2*)c2;
- (void)setmouse:(Point)p;
- (void)clearInput;
- (void)getmouse:(NSEvent*)e;
- (void)sendmouse:(int)b;
- (void)sendmouse:(int)b scroll:(int)scroll;
- (void)resetLastInputRect;
- (void)enlargeLastInputRect:(NSRect)r;
@end

@implementation DrawView
{
	NSMutableString	*_tmpText;
	NSRange		_markedRange;
	NSRange		_selectedRange;
	NSRect		_lastInputRect;	/* the view is flipped, this is not */
	BOOL		_tapping;
	int		_tapFingers;
	uint		_tapTime;
	BOOL		_inScrollPhase;
}

- (id)init
{
	LOG(@"View init");
	self = [super init];
	[self setAllowedTouchTypes:NSTouchTypeMaskDirect|NSTouchTypeMaskIndirect];
	_tmpText = [[NSMutableString alloc] initWithCapacity:2];
	_markedRange = NSMakeRange(NSNotFound, 0);
	_selectedRange = NSMakeRange(0, 0);
	_inScrollPhase = NO;
	return self;
}

- (CALayer*)makeBackingLayer { return [DrawLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

/*
 * rpc_attach allocates a new screen window with the given label and size
 * and attaches it to client c (by setting c->view).
 */
Memimage*
rpc_attach(Client *c, char *label, char *winsize)
{
	LOG(@"attachscreen(%s, %s)", label, winsize);

	c->impl = &macimpl;
	dispatch_sync(dispatch_get_main_queue(), ^(void){
		@autoreleasepool{
			DrawView *view;

			view = [[DrawView new] attach:c winsize:winsize label:label];
			[view initimg];
		}
	});
	return ((__bridge DrawView*)c->view).img;
}

- (id)attach:(Client*)client winsize:(char*)winsize label:(char*)label
{
	NSWindowStyleMask winstyle;
	NSRect sr, r;
	NSWindow *win;
	Rectangle wr;
	int haspos;
	NSArray<id<MTLDevice>> *devices;
	id<MTLDevice> device, d;
	DrawLayer *layer;
	CALayer *stub;

	winstyle = NSWindowStyleMaskClosable |
		NSWindowStyleMaskMiniaturizable |
		NSWindowStyleMaskResizable;
	if(label!=nil && *label!='\0')
		winstyle |= NSWindowStyleMaskTitled;

	sr = NSScreen.mainScreen.frame;
	r = NSScreen.mainScreen.visibleFrame;

	LOG(@"makewin(%s)", winsize);
	if(winsize==nil || *winsize=='\0'
	|| parsewinsize(winsize, &wr, &haspos)<0){
		wr = Rect(0, 0, sr.size.width*2/3, sr.size.height*2/3);
		haspos = 0;
	}

	r.origin.x = wr.min.x;
	r.origin.y = sr.size.height-wr.max.y;	/* winsize is top-left-based */
	r.size.width = fmin(Dx(wr), r.size.width);
	r.size.height = fmin(Dy(wr), r.size.height);
	r = [NSWindow contentRectForFrameRect:r styleMask:winstyle];

	win = [[NSWindow alloc]
		initWithContentRect:r
		styleMask:winstyle
		backing:NSBackingStoreBuffered
		defer:NO];
	[win setTitle:@"devdraw"];

	if(!haspos)
		[win center];
	[win setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
	[win setContentMinSize:NSMakeSize(64,64)];
	[win setOpaque:YES];
	[win setRestorable:NO];
	[win setAcceptsMouseMovedEvents:YES];

	client->view = CFBridgingRetain(self);
	self.client = client;
	self.currentCursor = nil;
	[win setContentView:self];	/* also sets self.window to win */
	[win setDelegate:self];
	[self setWantsLayer:YES];
	[self setLayerContentsRedrawPolicy:NSViewLayerContentsRedrawOnSetNeedsDisplay];

	device = nil;
	devices = MTLCopyAllDevices();
	for(d in devices)
		if(d.isLowPower && !d.isRemovable){
			device = d;
			break;
		}
	if(device == nil)
		device = MTLCreateSystemDefaultDevice();

	layer = (DrawLayer*)self.layer;
	self.dlayer = layer;
	layer.device = device;
	layer.cmd = [device newCommandQueue];
	layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	layer.framebufferOnly = YES;
	layer.opaque = YES;

	/*
	 * We use a default transparent layer on top of the CAMetalLayer.
	 * This seems to make fullscreen applications behave. Specifically,
	 * without this code if you enter full screen with Cmd-F, the screen
	 * goes black until the first mouse click.
	 */
	stub = [CALayer layer];
	stub.frame = CGRectMake(0, 0, 1, 1);
	[stub setNeedsDisplay];
	[layer addSublayer:stub];

	[NSEvent setMouseCoalescingEnabled:NO];

	[self topwin];
	[self setlabel:label];
	[self setcursor:nil cursor2:nil];

	return self;
}

/*
 * rpc_topwin moves the window to the top of the desktop.
 * Called from an RPC thread with no client lock held.
 */
static void
rpc_topwin(Client *c)
{
	DrawView *view;

	view = (__bridge DrawView*)c->view;
	dispatch_sync(dispatch_get_main_queue(), ^(void){
		[view topwin];
	});
}

- (void)topwin
{
	[self.window makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
}

/*
 * rpc_setlabel updates the client window's label.
 * If label == nil, the call is a no-op.
 * Called from an RPC thread with no client lock held.
 */
static void
rpc_setlabel(Client *client, char *label)
{
	DrawView *view;

	view = (__bridge DrawView*)client->view;
	dispatch_sync(dispatch_get_main_queue(), ^(void){
		[view setlabel:label];
	});
}

- (void)setlabel:(char*)label
{
	LOG(@"setlabel(%s)", label);
	if(label == nil)
		return;

	@autoreleasepool{
		NSString *s;

		s = [[NSString alloc] initWithUTF8String:label];
		[self.window setTitle:s];
		if(client0 != nil)
			[NSApp.dockTile setBadgeLabel:s];
	}
}

/*
 * rpc_setcursor updates the client window's cursor image. Either c and
 * c2 are both non-nil, or they are both nil to use the default arrow.
 * Called from an RPC thread with no client lock held.
 */
static void
rpc_setcursor(Client *client, Cursor *c, Cursor2 *c2)
{
	DrawView *view;

	view = (__bridge DrawView*)client->view;
	dispatch_sync(dispatch_get_main_queue(), ^(void){
		[view setcursor:c cursor2:c2];
	});
}

- (void)setcursor:(Cursor*)c cursor2:(Cursor2*)c2
{
	NSBitmapImageRep *r, *r2;
	NSImage *i;
	NSPoint p;
	uchar *plane[5], *plane2[5];
	uint b;

	if(c == nil){
		c = &bigarrow;
		c2 = &bigarrow2;
	}

	r = [[NSBitmapImageRep alloc]
		initWithBitmapDataPlanes:nil
		pixelsWide:16
		pixelsHigh:16
		bitsPerSample:1
		samplesPerPixel:2
		hasAlpha:YES
		isPlanar:YES
		colorSpaceName:NSDeviceWhiteColorSpace
		bytesPerRow:2
		bitsPerPixel:0];
	[r getBitmapDataPlanes:plane];
	for(b=0; b<nelem(c->set); b++){
		plane[0][b] = ~c->set[b] & c->clr[b];
		plane[1][b] = c->set[b] | c->clr[b];
	}

	r2 = [[NSBitmapImageRep alloc]
		initWithBitmapDataPlanes:nil
		pixelsWide:32
		pixelsHigh:32
		bitsPerSample:1
		samplesPerPixel:2
		hasAlpha:YES
		isPlanar:YES
		colorSpaceName:NSDeviceWhiteColorSpace
		bytesPerRow:4
		bitsPerPixel:0];
	[r2 getBitmapDataPlanes:plane2];
	for(b=0; b<nelem(c2->set); b++){
		plane2[0][b] = ~c2->set[b] & c2->clr[b];
		plane2[1][b] = c2->set[b] | c2->clr[b];
	}

	static BOOL debug = NO;
	if(debug){
		[[r representationUsingType:NSBitmapImageFileTypeBMP properties:@{}]
			writeToFile:@"/tmp/r.bmp" atomically:NO];
		[[r2 representationUsingType:NSBitmapImageFileTypeBMP properties:@{}]
			writeToFile:@"/tmp/r2.bmp" atomically:NO];
		debug = NO;
	}

	i = [[NSImage alloc] initWithSize:NSMakeSize(16, 16)];
	[i addRepresentation:r2];
	[i addRepresentation:r];

	p = NSMakePoint(-c->offset.x, -c->offset.y);
	self.currentCursor = [[NSCursor alloc] initWithImage:i hotSpot:p];
	[self.window invalidateCursorRectsForView:self];
}

- (void)initimg
{
	@autoreleasepool{
		CGFloat scale;
		NSSize size;
		MTLTextureDescriptor *textureDesc;

		size = [self convertSizeToBacking:self.bounds.size];
		LOG(@"initimg %.0f %.0f", size.width, size.height);

		self.img = allocmemimage(Rect(0, 0, size.width, size.height), XRGB32);
		if(self.img == nil)
			panic("allocmemimage: %r");
		if(self.img->data == nil)
			panic("img->data == nil");

		textureDesc = [MTLTextureDescriptor
			texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
			width:size.width
			height:size.height
			mipmapped:NO];
		textureDesc.allowGPUOptimizedContents = YES;
		textureDesc.usage = MTLTextureUsageShaderRead;
		textureDesc.cpuCacheMode = MTLCPUCacheModeWriteCombined;
		self.dlayer.texture = [self.dlayer.device newTextureWithDescriptor:textureDesc];

		scale = self.window.backingScaleFactor;
		[self.dlayer setDrawableSize:size];
		[self.dlayer setContentsScale:scale];

		/*
		 * NOTE: This is not really the display DPI. On retina, scale is 2;
		 * otherwise it is 1. This formula gives us 220 for retina, 110
		 * otherwise. That's not quite right but it's close to correct.
		 * https://en.wikipedia.org/wiki/Retina_display#Models
		 */
		self.client->displaydpi = scale * 110;
	}
}

/*
 * rpc_flush flushes changes to view.img's rectangle r
 * to the on-screen window, making them visible.
 * Called from an RPC thread with no client lock held.
 */
static void
rpc_flush(Client *client, Rectangle r)
{
	DrawView *view;

	view = (__bridge DrawView*)client->view;
	dispatch_async(dispatch_get_main_queue(), ^(void){
		[view flush:r];
	});
}

- (void)flush:(Rectangle)r
{
	@autoreleasepool{
		NSRect nr;
		dispatch_time_t time;

		if(!rectclip(&r, Rect(0, 0, self.dlayer.texture.width, self.dlayer.texture.height))
		|| !rectclip(&r, self.img->r))
			return;

		/*
		 * drawlk protects the pixel data in self.img. In addition to avoiding
		 * a technical data race, the lock avoids drawing partial updates,
		 * which makes animations like sweeping windows much less flickery.
		 */
		qlock(&drawlk);
		[self.dlayer.texture
			replaceRegion:MTLRegionMake2D(r.min.x, r.min.y, Dx(r), Dy(r))
			mipmapLevel:0
			withBytes:byteaddr(self.img, Pt(r.min.x, r.min.y))
			bytesPerRow:self.img->width*sizeof(u32int)];
		qunlock(&drawlk);

		nr = NSMakeRect(r.min.x, r.min.y, Dx(r), Dy(r));
		LOG(@"callsetNeedsDisplayInRect(%g, %g, %g, %g)", nr.origin.x, nr.origin.y, nr.size.width, nr.size.height);
		nr = [self.window convertRectFromBacking:nr];
		LOG(@"setNeedsDisplayInRect(%g, %g, %g, %g)", nr.origin.x, nr.origin.y, nr.size.width, nr.size.height);
		[self.dlayer setNeedsDisplayInRect:nr];

		time = dispatch_time(DISPATCH_TIME_NOW, 16 * NSEC_PER_MSEC);
		dispatch_after(time, dispatch_get_main_queue(), ^(void){
			[self.dlayer setNeedsDisplayInRect:nr];
		});

		[self enlargeLastInputRect:nr];
	}
}

/*
 * rpc_resizeimg forces the client window to discard its current window
 * and make a new one. It is called when the user types Cmd-R to toggle
 * whether retina mode is forced.
 * Called from an RPC thread with no client lock held.
 */
static void
rpc_resizeimg(Client *c)
{
	DrawView *view;

	view = (__bridge DrawView*)c->view;
	dispatch_async(dispatch_get_main_queue(), ^(void){
		[view resizeimg];
	});
}

- (void)resizeimg
{
	[self initimg];
	gfx_replacescreenimage(self.client, self.img);
}

- (void)windowDidResize:(NSNotification*)notif
{
	if(!self.inLiveResize && self.img!=nil)
		[self resizeimg];
}
- (void)viewDidEndLiveResize
{
	[super viewDidEndLiveResize];
	if(self.img != nil)
		[self resizeimg];
}

- (void)viewDidChangeBackingProperties
{
	[super viewDidChangeBackingProperties];
	if(self.img != nil)
		[self resizeimg];
}

/*
 * rpc_resizewindow asks for the client window to be resized to size r.
 * Called from an RPC thread with no client lock held.
 */
static void
rpc_resizewindow(Client *c, Rectangle r)
{
	DrawView *view;

	LOG(@"resizewindow %d %d %d %d", r.min.x, r.min.y, Dx(r), Dy(r));
	view = (__bridge DrawView*)c->view;
	dispatch_async(dispatch_get_main_queue(), ^(void){
		NSSize s;

		s = [view convertSizeFromBacking:NSMakeSize(Dx(r), Dy(r))];
		[view.window setContentSize:s];
	});
}

- (void)windowDidBecomeKey:(id)arg
{
	[self sendmouse:0];
}

- (void)windowDidResignKey:(id)arg
{
	gfx_abortcompose(self.client);
}

- (void)mouseMoved:(NSEvent*)e { [self getmouse:e]; }
- (void)mouseDown:(NSEvent*)e { [self getmouse:e]; }
- (void)mouseDragged:(NSEvent*)e { [self getmouse:e]; }
- (void)mouseUp:(NSEvent*)e { [self getmouse:e]; }
- (void)otherMouseDown:(NSEvent*)e { [self getmouse:e]; }
- (void)otherMouseDragged:(NSEvent*)e { [self getmouse:e]; }
- (void)otherMouseUp:(NSEvent*)e { [self getmouse:e]; }
- (void)rightMouseDown:(NSEvent*)e { [self getmouse:e]; }
- (void)rightMouseDragged:(NSEvent*)e { [self getmouse:e]; }
- (void)rightMouseUp:(NSEvent*)e { [self getmouse:e]; }

static CGFloat
roundhalfeven(CGFloat x)
{
	return nearbyint(x);
}

static int
scrolldeltatoint(CGFloat delta)
{
	int d;

	d = (int)roundhalfeven(delta);
	if(d == 0)
		d = (delta > 0) - (delta < 0);
	return d;
}

- (int)scrolldeltatobacking:(CGFloat)delta
{
	CGFloat d;

	d = [self convertSizeToBacking:NSMakeSize(0, fabs(delta))].height;
	return (int)roundhalfeven(copysign(d, delta));
}

- (void)scrollWheel:(NSEvent*)e
{
	NSEventPhase phase;
	BOOL inertial;
	CGFloat delta;

	phase = e.momentumPhase;
	inertial = (phase != NSEventPhaseNone);
	if(!inertial)
		phase = e.phase;
	delta = -e.scrollingDeltaY;

	if(phase != NSEventPhaseNone){
		if(phase == NSEventPhaseBegan){
			_inScrollPhase = YES;
			[self sendmouse:inertial ? Mscrollinertiastart : Mscrollmotionstart];
		}
		if(delta != 0)
			[self sendmouse:Mpixelscroll scroll:[self scrolldeltatobacking:delta]];
		if(_inScrollPhase && (phase==NSEventPhaseEnded || phase==NSEventPhaseCancelled)){
			_inScrollPhase = NO;
			[self sendmouse:inertial ? Mscrollinertiastop : Mscrollmotionstop];
		}
	}else if(delta != 0){
		if(e.hasPreciseScrollingDeltas)
			[self sendmouse:Mpixelscroll scroll:[self scrolldeltatobacking:delta]];
		else
			[self sendmouse:Mlinescroll scroll:scrolldeltatoint(delta)];
	}
}

- (void)keyDown:(NSEvent*)e
{
	LOG(@"keyDown to interpret");

	[self interpretKeyEvents:[NSArray arrayWithObject:e]];

	[self resetLastInputRect];
}

- (void)flagsChanged:(NSEvent*)e
{
	static NSEventModifierFlags omod;
	NSEventModifierFlags m;
	int b;

	LOG(@"flagsChanged");
	m = e.modifierFlags;

	b = mousebuttons();
	if(b != 0){
		if(m & ~omod & NSEventModifierFlagControl)
			b |= Mbutton1;
		if(m & ~omod & NSEventModifierFlagOption)
			b |= Mbutton2;
		if(m & ~omod & NSEventModifierFlagCommand)
			b |= Mbutton3;
		[self sendmouse:b];
	}else if(m & ~omod & NSEventModifierFlagOption)
		gfx_keystroke(self.client, Kalt);

	omod = m;
}

- (void)magnifyWithEvent:(NSEvent*)e
{
	CGFloat thr, mag;
	BOOL infull;

	thr = 0.05;
	mag = e.magnification;
	infull = !!(self.window.styleMask & NSWindowStyleMaskFullScreen);
	if(infull ? mag<=-thr : mag>=+thr)
		[self.window toggleFullScreen:nil];
}

- (void)touchesBeganWithEvent:(NSEvent*)e
{
	_tapping = YES;
	_tapFingers = [e touchesMatchingPhase:NSTouchPhaseTouching inView:nil].count;
	_tapTime = msec();
}
- (void)touchesMovedWithEvent:(NSEvent*)e
{
	_tapping = NO;
}
- (void)touchesEndedWithEvent:(NSEvent*)e
{
	if(_tapping
	&& [e touchesMatchingPhase:NSTouchPhaseTouching inView:nil].count==0
	&& msec()-_tapTime<250){
		switch(_tapFingers){
		case 3:
			[self sendmouse:Mbutton2];
			[self sendmouse:0];
			break;
		case 4:
			[self sendmouse:Mbutton2];
			[self sendmouse:Mbutton1];
			[self sendmouse:0];
			break;
		}
		_tapping = NO;
	}
}
- (void)touchesCancelledWithEvent:(NSEvent*)e
{
	_tapping = NO;
}

static int
mousebuttons(void)
{
	NSUInteger eb;
	int b;

	eb = NSEvent.pressedMouseButtons;
	b = 0;
	if(eb & 1)
		b |= Mbutton1;
	if(eb & 4)
		b |= Mbutton2;
	if(eb & 2)
		b |= Mbutton3;
	return b;
}

- (void)getmouse:(NSEvent*)e
{
	NSEventModifierFlags m;
	int b;

	b = mouseswap(mousebuttons());
	if(b == Mbutton1){
		m = e.modifierFlags;
		if(m & NSEventModifierFlagOption){
			gfx_abortcompose(self.client);
			b = Mbutton2;
		}else if(m & NSEventModifierFlagCommand)
			b = Mbutton3;
	}
	[self sendmouse:b];
}

- (void)sendmouse:(int)b
{
	[self sendmouse:b scroll:0];
}

- (void)sendmouse:(int)b scroll:(int)scroll
{
	NSPoint p;

	p = self.window.mouseLocationOutsideOfEventStream;
	p = [self.window convertPointToBacking:p];
	p.y = self.img->r.max.y - p.y;
	//LOG(@"(%d, %d) <- sendmouse(%d, %d)", (int)p.x, (int)p.y, b, scroll);

	gfx_mousetrack(self.client, (int)p.x, (int)p.y, b, scroll, msec());
	if(b!=0 && !NSIsEmptyRect(_lastInputRect))
		[self resetLastInputRect];
}

/*
 * rpc_setmouse moves the mouse cursor.
 * Called from an RPC thread with no client lock held.
 */
static void
rpc_setmouse(Client *c, Point p)
{
	DrawView *view;

	view = (__bridge DrawView*)c->view;
	dispatch_async(dispatch_get_main_queue(), ^(void){
		[view setmouse:p];
	});
}

- (void)setmouse:(Point)p
{
	@autoreleasepool{
		NSPoint q;

		LOG(@"setmouse(%d,%d)", p.x, p.y);
		q = [self.window convertPointFromBacking:NSMakePoint(p.x, p.y)];
		LOG(@"(%g, %g) <- fromBacking", q.x, q.y);
		q = [self convertPoint:q toView:nil];
		LOG(@"(%g, %g) <- toWindow", q.x, q.y);
		q = [self.window convertPointToScreen:q];
		LOG(@"(%g, %g) <- toScreen", q.x, q.y);
		/*
		 * Quartz has the origin of the "global display coordinate space"
		 * at the top left of the primary screen (not to be confused with
		 * the main screen) with y increasing downward, while Cocoa has the
		 * origin at the bottom left of the primary screen with y increasing
		 * upward. We flip the coordinate with a negative sign and shift
		 * upward by the height of the primary screen.
		 */
		q.y = NSScreen.screens[0].frame.size.height - q.y;
		LOG(@"(%g, %g) <- setmouse", q.x, q.y);
		CGWarpMouseCursorPosition(NSPointToCGPoint(q));
		CGAssociateMouseAndMouseCursorPosition(true);
	}
}

- (void)resetCursorRects
{
	[super resetCursorRects];
	[self addCursorRect:self.bounds cursor:self.currentCursor];
}

/* Conforms to the NSTextInputClient protocol. */
- (BOOL)hasMarkedText { return _markedRange.location != NSNotFound; }
- (NSRange)markedRange { return _markedRange; }
- (NSRange)selectedRange { return _selectedRange; }

- (void)setMarkedText:(id)string
	selectedRange:(NSRange)sRange
	replacementRange:(NSRange)rRange
{
	NSString *str;
	NSUInteger i;
	NSInteger l;

	LOG(@"setMarkedText: %@ (%lu, %lu) (%lu, %lu)", string,
		(ulong)sRange.location, (ulong)sRange.length,
		(ulong)rRange.location, (ulong)rRange.length);

	[self clearInput];

	if([string isKindOfClass:[NSAttributedString class]])
		str = [string string];
	else
		str = string;

	if(rRange.location == NSNotFound){
		if(_markedRange.location != NSNotFound)
			rRange = _markedRange;
		else
			rRange = _selectedRange;
	}

	if(str.length == 0){
		[_tmpText deleteCharactersInRange:rRange];
		[self unmarkText];
	}else{
		_markedRange = NSMakeRange(rRange.location, str.length);
		[_tmpText replaceCharactersInRange:rRange withString:str];
	}
	_selectedRange.location = rRange.location + sRange.location;
	_selectedRange.length = sRange.length;

	if(_tmpText.length != 0){
		LOG(@"text length %lu", (ulong)_tmpText.length);
		for(i=0; i<=_tmpText.length; i++){
			if(i == _markedRange.location)
				gfx_keystroke(self.client, '[');
			if(_selectedRange.length != 0){
				if(i == _selectedRange.location)
					gfx_keystroke(self.client, '{');
				if(i == NSMaxRange(_selectedRange))
					gfx_keystroke(self.client, '}');
			}
			if(i == NSMaxRange(_markedRange))
				gfx_keystroke(self.client, ']');
			if(i < _tmpText.length)
				gfx_keystroke(self.client, [_tmpText characterAtIndex:i]);
		}
		l = 1 + _tmpText.length - NSMaxRange(_selectedRange) +
			(_selectedRange.length > 0);
		LOG(@"move left %ld", (long)l);
		while(l-- > 0)
			gfx_keystroke(self.client, Kleft);
	}

	LOG(@"text: \"%@\"  (%lu,%lu)  (%lu,%lu)", _tmpText,
		(ulong)_markedRange.location, (ulong)_markedRange.length,
		(ulong)_selectedRange.location, (ulong)_selectedRange.length);
}

- (void)unmarkText
{
	NSUInteger len;
	//NSUInteger i;

	LOG(@"unmarkText");
	len = _tmpText.length;
	//for(i=0; i<len; i++)
	//	gfx_keystroke(self.client, [_tmpText characterAtIndex:i]);
	[_tmpText deleteCharactersInRange:NSMakeRange(0, len)];
	_markedRange = NSMakeRange(NSNotFound, 0);
	_selectedRange = NSMakeRange(0, 0);
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
	LOG(@"validAttributesForMarkedText");
	return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)r
	actualRange:(NSRangePointer)actualRange
{
	NSRange sr;
	NSAttributedString *s;

	LOG(@"attributedSubstringForProposedRange: (%lu, %lu) (%lu, %lu)",
		(ulong)r.location, (ulong)r.length,
		(ulong)actualRange->location, (ulong)actualRange->length);
	sr = NSMakeRange(0, _tmpText.length);
	sr = NSIntersectionRange(sr, r);
	if(actualRange != nil)
		*actualRange = sr;
	LOG(@"use range: %lu, %lu", (ulong)sr.location, (ulong)sr.length);
	s = nil;
	if(sr.length != 0)
		s = [[NSAttributedString alloc]
			initWithString:[_tmpText substringWithRange:sr]];
	LOG(@"	return %@", s);
	return s;
}

- (void)insertText:(id)s replacementRange:(NSRange)r
{
	NSUInteger i, len;

	LOG(@"insertText: %@ replacementRange: %lu, %lu",
		s, (ulong)r.location, (ulong)r.length);

	[self clearInput];

	len = [s length];
	for(i=0; i<len; i++)
		gfx_keystroke(self.client, [s characterAtIndex:i]);
	[_tmpText deleteCharactersInRange:NSMakeRange(0, _tmpText.length)];
	_markedRange = NSMakeRange(NSNotFound, 0);
	_selectedRange = NSMakeRange(0, 0);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
	LOG(@"characterIndexForPoint: %g, %g", point.x, point.y);
	return 0;
}

- (NSRect)firstRectForCharacterRange:(NSRange)r actualRange:(NSRangePointer)actualRange
{
	LOG(@"firstRectForCharacterRange: (%lu, %lu) (%lu, %lu)",
		(ulong)r.location, (ulong)r.length,
		(ulong)actualRange->location, (ulong)actualRange->length);
	if(actualRange != nil)
		*actualRange = r;
	return [self.window convertRectToScreen:_lastInputRect];
}

- (void)doCommandBySelector:(SEL)s
{
	NSEvent *e;
	NSEventModifierFlags m;
	uint c, k;

	LOG(@"doCommandBySelector (%@)", NSStringFromSelector(s));

	e = NSApp.currentEvent;
	c = [e.characters characterAtIndex:0];
	k = keycvt(c);
	LOG(@"keyDown: character0: 0x%x -> 0x%x", c, k);
	m = e.modifierFlags;

	if(m & NSEventModifierFlagCommand){
		if((m&NSEventModifierFlagShift) && 'a'<=k && k<='z')
			k += 'A' - 'a';
		if(' '<=k && k<='~')
			k += Kcmd;
	}
	if(k > 0)
		gfx_keystroke(self.client, k);
}

/* Helper for managing input rect approximately. */
- (void)resetLastInputRect
{
	LOG(@"resetLastInputRect");
	_lastInputRect.origin.x = 0.0;
	_lastInputRect.origin.y = 0.0;
	_lastInputRect.size.width = 0.0;
	_lastInputRect.size.height = 0.0;
}

- (void)enlargeLastInputRect:(NSRect)r
{
	r.origin.y = self.bounds.size.height - r.origin.y - r.size.height;
	_lastInputRect = NSUnionRect(_lastInputRect, r);
	LOG(@"update last input rect (%g, %g, %g, %g)",
		_lastInputRect.origin.x, _lastInputRect.origin.y,
		_lastInputRect.size.width, _lastInputRect.size.height);
}

- (void)clearInput
{
	NSInteger l;

	if(_tmpText.length == 0)
		return;
	l = 1 + _tmpText.length - NSMaxRange(_selectedRange) +
		(_selectedRange.length > 0);
	LOG(@"move right %ld", (long)l);
	while(l-- > 0)
		gfx_keystroke(self.client, Kright);
	l = _tmpText.length + 2 + 2*(_selectedRange.length>0);
	LOG(@"backspace %ld", (long)l);
	while(l-- > 0)
		gfx_keystroke(self.client, Kbs);
}

- (NSApplicationPresentationOptions)window:(id)arg
	willUseFullScreenPresentationOptions:(NSApplicationPresentationOptions)o
{
	/*
	 * The default for full-screen is to auto-hide the dock and menu bar,
	 * but the menu bar in particular comes back when the cursor is just
	 * near the top of the screen, which makes acme's top tag line very
	 * difficult to use. Disable the menu bar entirely. In theory this
	 * code disables the dock entirely too, but if you drag the mouse down
	 * far enough off the bottom of the screen the dock still unhides.
	 * That's OK.
	 */
	o &= ~NSApplicationPresentationAutoHideDock;
	o &= ~NSApplicationPresentationAutoHideMenuBar;
	o |= NSApplicationPresentationHideDock;
	o |= NSApplicationPresentationHideMenuBar;
	return o;
}

- (void)windowWillEnterFullScreen:(NSNotification*)notif
{
	/*
	 * This is a heavier-weight way to make sure the menu bar and dock go
	 * away, but this affects all screens even though the app is running on
	 * full screen on only one screen, so it's not great. The behavior from
	 * the willUseFullScreenPresentationOptions seems to be enough for now.
	 */
	//[NSApp setPresentationOptions:
	//	NSApplicationPresentationHideMenuBar|
	//	NSApplicationPresentationHideDock];
}

- (void)windowDidExitFullScreen:(NSNotification*)notif
{
	//[NSApp setPresentationOptions:NSApplicationPresentationDefault];
}
@end

static uint
msec(void)
{
	return nsec()/1000000;
}

static uint
keycvt(uint code)
{
	switch(code){
	case '\r': return '\n';
	case 127: return '\b';
	case NSUpArrowFunctionKey: return Kup;
	case NSDownArrowFunctionKey: return Kdown;
	case NSLeftArrowFunctionKey: return Kleft;
	case NSRightArrowFunctionKey: return Kright;
	case NSInsertFunctionKey: return Kins;
	case NSDeleteFunctionKey: return Kdel;
	case NSHomeFunctionKey: return Khome;
	case NSEndFunctionKey: return Kend;
	case NSPageUpFunctionKey: return Kpgup;
	case NSPageDownFunctionKey: return Kpgdown;
	case NSF1FunctionKey: return KF|1;
	case NSF2FunctionKey: return KF|2;
	case NSF3FunctionKey: return KF|3;
	case NSF4FunctionKey: return KF|4;
	case NSF5FunctionKey: return KF|5;
	case NSF6FunctionKey: return KF|6;
	case NSF7FunctionKey: return KF|7;
	case NSF8FunctionKey: return KF|8;
	case NSF9FunctionKey: return KF|9;
	case NSF10FunctionKey: return KF|10;
	case NSF11FunctionKey: return KF|11;
	case NSF12FunctionKey: return KF|12;
	case NSBeginFunctionKey:
	case NSPrintScreenFunctionKey:
	case NSScrollLockFunctionKey:
	case NSF13FunctionKey:
	case NSF14FunctionKey:
	case NSF15FunctionKey:
	case NSF16FunctionKey:
	case NSF17FunctionKey:
	case NSF18FunctionKey:
	case NSF19FunctionKey:
	case NSF20FunctionKey:
	case NSF21FunctionKey:
	case NSF22FunctionKey:
	case NSF23FunctionKey:
	case NSF24FunctionKey:
	case NSF25FunctionKey:
	case NSF26FunctionKey:
	case NSF27FunctionKey:
	case NSF28FunctionKey:
	case NSF29FunctionKey:
	case NSF30FunctionKey:
	case NSF31FunctionKey:
	case NSF32FunctionKey:
	case NSF33FunctionKey:
	case NSF34FunctionKey:
	case NSF35FunctionKey:
	case NSPauseFunctionKey:
	case NSSysReqFunctionKey:
	case NSBreakFunctionKey:
	case NSResetFunctionKey:
	case NSStopFunctionKey:
	case NSMenuFunctionKey:
	case NSUserFunctionKey:
	case NSSystemFunctionKey:
	case NSPrintFunctionKey:
	case NSClearLineFunctionKey:
	case NSClearDisplayFunctionKey:
	case NSInsertLineFunctionKey:
	case NSDeleteLineFunctionKey:
	case NSInsertCharFunctionKey:
	case NSDeleteCharFunctionKey:
	case NSPrevFunctionKey:
	case NSNextFunctionKey:
	case NSSelectFunctionKey:
	case NSExecuteFunctionKey:
	case NSUndoFunctionKey:
	case NSRedoFunctionKey:
	case NSFindFunctionKey:
	case NSHelpFunctionKey:
	case NSModeSwitchFunctionKey: return 0;
	default: return code;
	}
}

/*
 * rpc_getsnarf reads the current pasteboard as a plain text string.
 * Called from an RPC thread with no client lock held.
 */
char*
rpc_getsnarf(void)
{
	char __block *ret;

	ret = nil;
	dispatch_sync(dispatch_get_main_queue(), ^(void){
		@autoreleasepool{
			NSPasteboard *pb;
			NSString *s;

			pb = NSPasteboard.generalPasteboard;
			s = [pb stringForType:NSPasteboardTypeString];
			if(s != nil)
				ret = strdup((char*)s.UTF8String);
		}
	});
	return ret;
}

/*
 * rpc_putsnarf writes the given text to the pasteboard.
 * Called from an RPC thread with no client lock held.
 */
void
rpc_putsnarf(char *s)
{
	if(s==nil || strlen(s)>=SnarfSize)
		return;

	dispatch_sync(dispatch_get_main_queue(), ^(void){
		@autoreleasepool{
			NSArray *t;
			NSPasteboard *pb;
			NSString *str;

			t = [NSArray arrayWithObject:NSPasteboardTypeString];
			pb = NSPasteboard.generalPasteboard;
			str = [[NSString alloc] initWithUTF8String:s];
			[pb declareTypes:t owner:nil];
			[pb setString:str forType:NSPasteboardTypeString];
		}
	});
}

/*
 * rpc_bouncemouse is for sending a mouse event
 * back to the X11 window manager rio(1).
 * Does not apply here.
 */
static void
rpc_bouncemouse(Client *c, Mouse m)
{
}

/*
 * We don't use the graphics thread state during memimagedraw,
 * so rpc_gfxdrawlock and rpc_gfxdrawunlock are no-ops.
 */
void
rpc_gfxdrawlock(void)
{
}

void
rpc_gfxdrawunlock(void)
{
}

/*
 * The setprocname() function below is an adaptation of SetProcessName()
 * from Chromium's mac_util.mm, licensed under the following terms. The
 * original source can be found at
 * https://src.chromium.org/viewvc/chrome/trunk/src/base/mac/mac_util.mm?pathrev=227366#l301
 *
 * Copyright (c) 2013 The Chromium Authors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *    * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
static void
setprocname(char *procname)
{
	/*
	 * Warning: here be dragons! This is SPI reverse-engineered from WebKit's
	 * plugin host, and could break at any time (although realistically it's
	 * only likely to break in a new major release).
	 * When 10.7 is available, check that this still works, and update this
	 * comment for 10.8.
	 */

	/* Private CFType used in these LaunchServices calls. */
	typedef CFTypeRef PrivateLSASN;
	typedef PrivateLSASN (*LSGetCurrentApplicationASNType)();
	typedef OSStatus (*LSSetApplicationInformationItemType)(
		int, PrivateLSASN, CFStringRef, CFStringRef, CFDictionaryRef*);

	static LSGetCurrentApplicationASNType ls_get_current_application_asn_func;
	static LSSetApplicationInformationItemType ls_set_application_information_item_func;
	static CFStringRef ls_display_name_key;
	static bool did_symbol_lookup;

	/* Constant used by WebKit; what exactly it means is unknown. */
	enum { magic_session_constant = -2 };

	CFBundleRef launch_services_bundle;
	CFStringRef *key_pointer;
	ProcessSerialNumber psn;
	PrivateLSASN asn;
	CFStringRef process_name;
	OSErr err;

	if(!procname || *procname=='\0'){
		fprint(2, "setprocname: empty process name\n");
		return;
	}
	if(!NSThread.isMainThread){
		fprint(2, "setprocname: not in main thread\n");
		return;
	}

	if(!did_symbol_lookup){
		did_symbol_lookup = true;
		launch_services_bundle = CFBundleGetBundleWithIdentifier(
			CFSTR("com.apple.LaunchServices"));
		if(launch_services_bundle == nil){
			fprint(2, "setprocname: failed to look up LaunchServices bundle\n");
			return;
		}

		ls_get_current_application_asn_func = CFBundleGetFunctionPointerForName(
			launch_services_bundle, CFSTR("_LSGetCurrentApplicationASN"));
		if(ls_get_current_application_asn_func == nil)
			fprint(2, "setprocname: could not find _LSGetCurrentApplicationASN\n");

		ls_set_application_information_item_func = CFBundleGetFunctionPointerForName(
			launch_services_bundle, CFSTR("_LSSetApplicationInformationItem"));
		if(ls_set_application_information_item_func == nil)
			fprint(2, "setprocname: could not find _LSSetApplicationInformationItem\n");

		key_pointer = CFBundleGetDataPointerForName(launch_services_bundle,
			CFSTR("_kLSDisplayNameKey"));
		ls_display_name_key = key_pointer ? *key_pointer : nil;
		if(ls_display_name_key == nil)
			fprint(2, "setprocname: could not find _kLSDisplayNameKey\n");

		/*
		 * Internally, this call relies on the Mach ports that are started up by the
		 * Carbon Process Manager. In debug builds this usually happens due to how
		 * the logging layers are started up; but in release, it isn't started in as
		 * much of a defined order. So if the symbols had to be loaded, go ahead
		 * and force a call to make sure the manager has been initialized and hence
		 * the ports are opened.
		 */
		GetCurrentProcess(&psn);
	}
	if(!ls_get_current_application_asn_func
	|| !ls_set_application_information_item_func
	|| !ls_display_name_key)
		return;

	process_name = CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
		procname, kCFStringEncodingUTF8, kCFAllocatorNull);
	if(process_name == nil){
		fprint(2, "setprocname: failed to make a string for the process name\n");
		return;
	}

	asn = ls_get_current_application_asn_func();
	err = ls_set_application_information_item_func(magic_session_constant,
		asn, ls_display_name_key, process_name, nil /* optional out param */);
	if(err != noErr)
		fprint(2, "setprocname: call to set process name failed\n");

	CFRelease(process_name);
}
