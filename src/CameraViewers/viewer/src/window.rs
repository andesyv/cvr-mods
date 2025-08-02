use std::sync::Arc;
use std::time::Instant;

use crate::render_context::{DrawResult, RenderContext, RenderContextCreationInfo};
use crate::{HEIGHT, WIDTH};
use winit::application::ApplicationHandler;
use winit::dpi::PhysicalSize;
use winit::event::{ElementState, KeyEvent, WindowEvent};
use winit::event_loop::ActiveEventLoop;
use winit::keyboard::{Key, NamedKey};
use winit::window::WindowId;

pub struct Window {
    close_requested: bool,
    inner_window: Option<Arc<winit::window::Window>>,
    context_creation_info: RenderContextCreationInfo,
    context: Option<RenderContext>,
    app_timer: Instant,
}

impl Window {
    pub fn new(render_context_creation_info: RenderContextCreationInfo) -> Self {
        Self {
            close_requested: false,
            inner_window: None,
            context_creation_info: render_context_creation_info,
            context: None,
            app_timer: Instant::now(),
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
                match self.context.as_mut().unwrap().draw(t) {
                    DrawResult::WaitingForHost => {
                        self.inner_window.as_ref().unwrap().request_redraw()
                    }
                    DrawResult::HostDisconnected => self.close_requested = true,
                    // DrawResult::SwapchainOutdated => {} // TODO
                    _ => (),
                }
            }
            // WindowEvent::Resized(_) => {
            //
            // }
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
