
Not getting love from this yet. I have been able, apparently by sheer luck, get a colored quad onto the Rift, but it flickers, and disappears if I try to do anything more fancy either in the external or in the patcher.

Some observations:

- to render the scene in Jitter we need a jit.gl.render
	- is this true? Is there a way to create a 'fake' jitter context (actually Rift texturesets) 
	
- A problem this raises is that everything gets synced to the main display, including a stall that the rift doesn't need. This kills VR.
	- Potentially we could work around this by running the Rift external stuff in a separate thread.
		- Notes: https://developer.oculus.com/documentation/pcsdk/latest/concepts/dg-render/#dg_multi_thread_engine_update_render
		

Q: How is Cinder Rift achieving it? Why does rendering in update() work?
- Nothing happens in draw() (equiv. having nothing in jit.gl.render scene...)
- In update(), hmd::ScopedRiftBuffer bind{ mRift }; mRift->enableEye(eye); and then draw scene.