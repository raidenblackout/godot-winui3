/**************************************************************************/
/*  rendering_context_driver_d3d12.h                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "drivers/d3d12/rendering_device_driver_d3d12.h"
#include "servers/display/display_server_enums.h"
#include "servers/rendering/rendering_context_driver.h"

#include <drivers/d3d12/godot_d3dx12.h>

#ifdef DCOMP_ENABLED
#include <dcomp.h>
#endif

#ifdef WINUI3_ENABLED
struct ISwapChainPanelNative;
#endif

#include <wrl/client.h>

#define ARRAY_SIZE(a) std_size(a)

class RenderingContextDriverD3D12 : public RenderingContextDriver {
	Microsoft::WRL::ComPtr<ID3D12DeviceFactory> device_factory;
	Microsoft::WRL::ComPtr<IDXGIFactory2> dxgi_factory;
	TightLocalVector<Device> driver_devices;
	bool tearing_supported = false;

	Error _init_device_factory();
	Error _initialize_debug_layers();
	Error _create_dxgi_factory();
	Error _initialize_devices();

public:
	virtual Error initialize() override;
	virtual const Device &device_get(uint32_t p_device_index) const override;
	virtual uint32_t device_get_count() const override;
	virtual bool device_supports_present(uint32_t p_device_index, SurfaceID p_surface) const override;
	virtual RenderingDeviceDriver *driver_create() override;
	virtual void driver_free(RenderingDeviceDriver *p_driver) override;
	virtual SurfaceID surface_create(const void *p_platform_data) override;
	virtual void surface_set_size(SurfaceID p_surface, uint32_t p_width, uint32_t p_height) override;
	virtual void surface_set_vsync_mode(SurfaceID p_surface, DisplayServerEnums::VSyncMode p_vsync_mode) override;
	virtual DisplayServerEnums::VSyncMode surface_get_vsync_mode(SurfaceID p_surface) const override;
	virtual void surface_set_hdr_output_enabled(SurfaceID p_surface, bool p_enabled) override;
	virtual bool surface_get_hdr_output_enabled(SurfaceID p_surface) const override;
	virtual void surface_set_hdr_output_reference_luminance(SurfaceID p_surface, float p_reference_luminance) override;
	virtual float surface_get_hdr_output_reference_luminance(SurfaceID p_surface) const override;
	virtual void surface_set_hdr_output_max_luminance(SurfaceID p_surface, float p_max_luminance) override;
	virtual float surface_get_hdr_output_max_luminance(SurfaceID p_surface) const override;
	virtual void surface_set_hdr_output_linear_luminance_scale(SurfaceID p_surface, float p_linear_luminance_scale) override;
	virtual float surface_get_hdr_output_linear_luminance_scale(SurfaceID p_surface) const override;
	virtual float surface_get_hdr_output_max_value(SurfaceID p_surface) const override;
	virtual uint32_t surface_get_width(SurfaceID p_surface) const override;
	virtual uint32_t surface_get_height(SurfaceID p_surface) const override;
	virtual void surface_set_needs_resize(SurfaceID p_surface, bool p_needs_resize) override;
	virtual bool surface_get_needs_resize(SurfaceID p_surface) const override;
#ifdef WINUI3_ENABLED
	void surface_set_composition_scale(SurfaceID p_surface, float p_scale_x, float p_scale_y);
#endif
	virtual void surface_destroy(SurfaceID p_surface) override;
	virtual bool is_debug_utils_enabled() const override;

	// Platform-specific data for the Windows embedded in this driver.
	// Note: keep all members trivially default-constructible — this struct is
	// used inside an anonymous union in display_server_windows.cpp, so a
	// non-trivial default ctor (e.g. via a default member initializer) would
	// delete the union's default ctor. Callers must set every field they care
	// about explicitly before passing this to surface_create().
#ifdef WINUI3_ENABLED
	// Runs p_work(p_ctx) synchronously on the thread that owns the SwapChainPanel
	// (the host UI thread). Supplied by the WinUI3 host when the engine iterates
	// on a dedicated thread; nullptr means "run inline" (engine on the UI thread).
	typedef void (*SwapChainBindDispatch)(void (*p_work)(void *p_ctx), void *p_ctx);
#endif

	struct WindowPlatformData {
		HWND window;
#ifdef WINUI3_ENABLED
		ISwapChainPanelNative *swap_chain_panel;
		SwapChainBindDispatch ui_dispatch;
#endif
	};

	// D3D12-only methods.
	struct Surface {
		HWND hwnd = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;
		DisplayServerEnums::VSyncMode vsync_mode = DisplayServerEnums::VSYNC_ENABLED;
		bool needs_resize = false;

		bool hdr_output = false;
		// BT.2408 recommendation of 203 nits for HDR Reference White, rounded to 200
		// to be a more pleasant player-facing value. This value is used by Steam
		// Deck and other Windows emulation that does not provide an SDRWhiteLevel.
		float hdr_reference_luminance = 200.0f;
		float hdr_max_luminance = 1000.0f;
		float hdr_linear_luminance_scale = 80.0f;

#ifdef DCOMP_ENABLED
		Microsoft::WRL::ComPtr<IDCompositionDevice> composition_device;
		Microsoft::WRL::ComPtr<IDCompositionTarget> composition_target;
		Microsoft::WRL::ComPtr<IDCompositionVisual> composition_visual;
#endif
#ifdef WINUI3_ENABLED
		ISwapChainPanelNative *swap_chain_panel = nullptr;
		// Marshals the panel-affine bind (SetSwapChain / SetMatrixTransform) onto
		// the UI thread; nullptr means run inline. See WindowPlatformData.
		SwapChainBindDispatch ui_dispatch = nullptr;
		// CompositionScale of the SwapChainPanel — physical-pixels-per-DIP. The
		// swap chain must apply the inverse of this as a transform so that the
		// pixel-sized buffer maps 1:1 into the panel's DIP-sized output area.
		float composition_scale_x = 1.0f;
		float composition_scale_y = 1.0f;
#endif
	};

	HMODULE lib_d3d12 = nullptr;
	HMODULE lib_dxgi = nullptr;
#ifdef DCOMP_ENABLED
	HMODULE lib_dcomp = nullptr;
#endif

	IDXGIAdapter1 *create_adapter(uint32_t p_adapter_index) const;
	ID3D12DeviceFactory *device_factory_get() const;
	IDXGIFactory2 *dxgi_factory_get();
	bool get_tearing_supported() const;
	bool use_validation_layers() const;

	RenderingContextDriverD3D12();
	virtual ~RenderingContextDriverD3D12() override;
};
