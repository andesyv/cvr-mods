use crate::render_context::{DrawResult, RenderContext, RenderContextCreationInfo};
use crate::{HEIGHT, WIDTH};
use std::sync::Arc;
use std::time::Instant;
use winit::application::ApplicationHandler;
use winit::dpi::PhysicalSize;
use winit::event::{ElementState, KeyEvent, WindowEvent};
use winit::event_loop::ActiveEventLoop;
use winit::keyboard::{Key, NamedKey};
use winit::window::WindowId;

struct FrameTimeMeasurements {
    pub frame_count: usize,
    last_frametime_measurement: Instant,
    frames_per_second: f32,
}

impl Default for FrameTimeMeasurements {
    fn default() -> Self {
        Self {
            frame_count: 0,
            last_frametime_measurement: Instant::now(),
            frames_per_second: 0.0,
        }
    }
}

impl FrameTimeMeasurements {
    pub fn calc_current_average(&mut self) -> f32 {
        if self.frame_count > 30 {
            let frames_per_second = (self.frame_count as f64
                / self.last_frametime_measurement.elapsed().as_secs_f64())
                as f32;
            self.last_frametime_measurement = Instant::now();
            self.frame_count = 0;
            self.frames_per_second = frames_per_second;
        }
        self.frames_per_second
    }

    pub fn increment_frame_count(&mut self) {
        self.frame_count += 1;
    }
}

pub struct Window {
    close_requested: bool,
    inner_window: Option<Arc<winit::window::Window>>,
    context_creation_info: RenderContextCreationInfo,
    context: Option<RenderContext>,
    swapchain_outdated: bool,
    app_timer: Instant,
    frame_time_measurements: FrameTimeMeasurements,
}

impl Window {
    pub fn new(render_context_creation_info: RenderContextCreationInfo) -> Self {
        Self {
            close_requested: false,
            inner_window: None,
            context_creation_info: render_context_creation_info,
            context: None,
            swapchain_outdated: false,
            app_timer: Instant::now(),
            frame_time_measurements: Default::default(),
        }
    }
}

impl ApplicationHandler for Window {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        let window_attributes = winit::window::Window::default_attributes()
            .with_title(env!("CARGO_PKG_NAME"))
            .with_inner_size(PhysicalSize::new(800, 600));
        let window = Arc::new(
            event_loop
                .create_window(window_attributes)
                .expect("Failed to create window"),
        );
        self.inner_window = Some(window.clone());
        self.context = Some(RenderContext::new(
            event_loop,
            window,
            [WIDTH, HEIGHT],
            std::mem::take(&mut self.context_creation_info),
        ));
        if cfg!(debug_assertions) {
            println!(
                "Setup took {} milliseconds to complete",
                self.app_timer.elapsed().as_millis()
            );
        }
    }

    fn window_event(
        &mut self,
        _event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        if let Some(context) = &mut self.context
            && context.gui().update(&event)
        {
            return;
        }

        match &event {
            WindowEvent::CloseRequested => self.close_requested = true,
            WindowEvent::KeyboardInput {
                event:
                    KeyEvent {
                        logical_key: key,
                        state: ElementState::Pressed,
                        ..
                    },
                ..
            } => {
                if let Key::Named(NamedKey::Escape) = key.as_ref() {
                    self.close_requested = true;
                }
            }
            WindowEvent::RedrawRequested => {
                let PhysicalSize { width, height } =
                    self.inner_window.as_ref().unwrap().inner_size();
                if self.close_requested || width == 0 || height == 0 {
                    return;
                }

                let t = self.app_timer.elapsed().as_secs_f32();
                match self
                    .context
                    .as_mut()
                    .unwrap()
                    .draw(t, Some(self.frame_time_measurements.calc_current_average()))
                {
                    DrawResult::WaitingForHost => {
                        self.inner_window.as_ref().unwrap().request_redraw()
                    }
                    DrawResult::HostDisconnected => {
                        self.close_requested = true;
                        return;
                    }
                    result => {
                        if let DrawResult::SwapchainOutdated = result {
                            self.swapchain_outdated = true;
                        }
                        self.frame_time_measurements.increment_frame_count();
                    }
                }

                if self.swapchain_outdated {
                    self.context
                        .as_mut()
                        .unwrap()
                        .recreate_swapchain(width, height);
                    self.swapchain_outdated = false;
                }
            }
            WindowEvent::Resized(_) => {
                self.swapchain_outdated = true;
            }
            _ => (),
        }
    }

    fn about_to_wait(&mut self, event_loop: &ActiveEventLoop) {
        if self.close_requested {
            event_loop.exit();
        } else {
            self.inner_window.as_ref().unwrap().request_redraw();
        }
    }
}
