use std::sync::Arc;
use std::time::{Duration, Instant};

use crate::render_context::RenderContext;
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
    context: Option<RenderContext>,
    app_timer: Instant,
    first_frame: bool,
}

impl Default for Window {
    fn default() -> Self {
        Self {
            close_requested: false,
            inner_window: None,
            context: None,
            app_timer: Instant::now(),
            first_frame: true,
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
        self.context = Some(RenderContext::new(event_loop, window, [WIDTH, HEIGHT]));
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
            } => match key.as_ref() {
                Key::Named(NamedKey::Escape) => {
                    self.close_requested = true;
                }
                _ => (),
            },
            WindowEvent::RedrawRequested => {
                self.context
                    .as_mut()
                    .unwrap()
                    .draw(self.app_timer.elapsed().as_secs_f32());
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
            // We want to give the parent process a bit of initial time to finish setup, so we wait
            // a bit before rendering the first frame.
            if self.first_frame {
                assert!(self.context.as_ref().unwrap().memory_exporter.is_valid());
                std::thread::sleep(Duration::from_secs(30));
                self.first_frame = false;
            }
            self.inner_window.as_ref().unwrap().request_redraw();
        }
    }
}
