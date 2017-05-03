{
	"patcher" : 	{
		"fileversion" : 1,
		"appversion" : 		{
			"major" : 7,
			"minor" : 3,
			"revision" : 2,
			"architecture" : "x64",
			"modernui" : 1
		}
,
		"rect" : [ 670.0, 94.0, 907.0, 892.0 ],
		"bglocked" : 0,
		"openinpresentation" : 0,
		"default_fontsize" : 12.0,
		"default_fontface" : 0,
		"default_fontname" : "Arial",
		"gridonopen" : 2,
		"gridsize" : [ 22.0, 22.0 ],
		"gridsnaponopen" : 2,
		"objectsnaponopen" : 1,
		"statusbarvisible" : 2,
		"toolbarvisible" : 0,
		"lefttoolbarpinned" : 0,
		"toptoolbarpinned" : 0,
		"righttoolbarpinned" : 0,
		"bottomtoolbarpinned" : 0,
		"toolbars_unpinned_last_save" : 0,
		"tallnewobj" : 0,
		"boxanimatetime" : 200,
		"enablehscroll" : 1,
		"enablevscroll" : 1,
		"devicewidth" : 0.0,
		"description" : "",
		"digest" : "",
		"tags" : "",
		"style" : "",
		"subpatcher_template" : "",
		"boxes" : [ 			{
				"box" : 				{
					"id" : "obj-75",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 198.0, 220.0, 257.0, 20.0 ],
					"style" : "",
					"text" : "(first turned on controller = right, second = left)"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-73",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 792.0, 163.0, 20.0 ],
					"style" : "",
					"text" : "send messages to jit.window"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.646639, 0.821777, 0.854593, 1.0 ],
					"id" : "obj-71",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 792.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/window"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-67",
					"linecount" : 2,
					"maxclass" : "message",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 676.0, 792.0, 202.0, 36.0 ],
					"style" : "",
					"text" : ";\r\n/vr/window size 400 400, pos 10 40"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-68",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 88.0, 838.0, 190.0, 20.0 ],
					"style" : "",
					"text" : "freeze HMD position & orientation"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-65",
					"maxclass" : "toggle",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "int" ],
					"parameter_enable" : 0,
					"patching_rect" : [ 66.0, 836.0, 24.0, 24.0 ],
					"style" : ""
				}

			}
, 			{
				"box" : 				{
					"color" : [ 0.702269, 0.811747, 0.303388, 1.0 ],
					"id" : "obj-66",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 862.0, 110.0, 22.0 ],
					"style" : "",
					"text" : "s /vr/HMD/freeze"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-64",
					"maxclass" : "live.line",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 44.0, 616.0, 834.0, 5.0 ]
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-16",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 176.0, 94.0, 20.0 ],
					"style" : "",
					"text" : "angular velocity"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-20",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 154.0, 51.0, 20.0 ],
					"style" : "",
					"text" : "velocity"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-33",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 462.0, 176.0, 33.0, 20.0 ],
					"style" : "",
					"text" : "Roll"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-34",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 417.0, 176.0, 33.0, 20.0 ],
					"style" : "",
					"text" : "Yaw"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-38",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 374.0, 176.0, 41.0, 20.0 ],
					"style" : "",
					"text" : "Pitch"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-39",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 374.0, 154.0, 18.0, 20.0 ],
					"style" : "",
					"text" : "Z"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-60",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 352.0, 154.0, 18.0, 20.0 ],
					"style" : "",
					"text" : "Y"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-61",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 330.0, 154.0, 18.0, 20.0 ],
					"style" : "",
					"text" : "X"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-62",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 154.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/HMD/velocity"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-63",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 176.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/HMD/angular_velocity"
				}

			}
, 			{
				"box" : 				{
					"dontreplace" : 1,
					"id" : "obj-18",
					"maxclass" : "message",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 700.0, 132.0, 176.0, 22.0 ],
					"style" : "",
					"text" : "0.207768 1.09662 0.527205"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-1",
					"maxclass" : "newobj",
					"numinlets" : 0,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 766.0, 88.0, 110.0, 22.0 ],
					"style" : "",
					"text" : "r /vr/HMD/position"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-3",
					"linecount" : 2,
					"maxclass" : "message",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 726.0, 666.0, 157.0, 36.0 ],
					"style" : "",
					"text" : ";\r\n/vr/desktop-display/stereo 0"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-2",
					"linecount" : 2,
					"maxclass" : "message",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 726.0, 710.0, 157.0, 36.0 ],
					"style" : "",
					"text" : ";\r\n/vr/desktop-display/stereo 1"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-5",
					"maxclass" : "newobj",
					"numinlets" : 0,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 748.0, 506.0, 106.0, 22.0 ],
					"style" : "",
					"text" : "r /vr/render/bang"
				}

			}
, 			{
				"box" : 				{
					"fontface" : 0,
					"fontname" : "Arial",
					"fontsize" : 12.0,
					"id" : "obj-4",
					"maxclass" : "jit.fpsgui",
					"numinlets" : 1,
					"numoutlets" : 2,
					"outlettype" : [ "", "" ],
					"patching_rect" : [ 748.0, 530.0, 80.0, 36.0 ],
					"style" : ""
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.358573, 0.333383, 0.3663, 0.29 ],
					"id" : "obj-59",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 22.0, 22.0, 193.0, 20.0 ],
					"style" : "",
					"text" : "RECEIVE messages from vr.world"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-58",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 704.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "enable fullscreen display"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.646639, 0.821777, 0.854593, 1.0 ],
					"id" : "obj-57",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 704.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/desktop-display/fullscreen"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-56",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 440.0, 105.0, 20.0 ],
					"style" : "",
					"text" : "battery state (0-1)"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-55",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 440.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/battery"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-54",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 748.0, 282.0, 20.0 ],
					"style" : "",
					"text" : "enable sending captured texture to htcvive external"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.646639, 0.821777, 0.854593, 1.0 ],
					"id" : "obj-53",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 748.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/gl.node/direct"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-52",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 660.0, 431.0, 20.0 ],
					"style" : "",
					"text" : "send messages directly to htcvive external (near_clip, far_clip, use_camera, ...)"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.646639, 0.821777, 0.854593, 1.0 ],
					"id" : "obj-51",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 660.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-50",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 770.0, 495.0, 20.0 ],
					"style" : "",
					"text" : "send messages to jit.anim.node (set position/orientation of world origin (e.g. for navigation))"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.646639, 0.821777, 0.854593, 1.0 ],
					"id" : "obj-49",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 770.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/anim.node"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-48",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 726.0, 259.0, 20.0 ],
					"style" : "",
					"text" : "send messages to jit.gl.node (named vr_world)"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.646639, 0.821777, 0.854593, 1.0 ],
					"id" : "obj-47",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 726.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/gl.node"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-46",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 682.0, 317.0, 20.0 ],
					"style" : "",
					"text" : "0 = desktop display show only 1 eye - 1 = show both eyes"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.646639, 0.821777, 0.854593, 1.0 ],
					"id" : "obj-45",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 682.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/desktop-display/stereo"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.358573, 0.333383, 0.3663, 0.29 ],
					"id" : "obj-44",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 22.0, 638.0, 160.0, 20.0 ],
					"style" : "",
					"text" : "SEND messages to vr.world"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-43",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 572.0, 614.0, 20.0 ],
					"style" : "",
					"text" : "send a bang on each frame (after all controllers & tracking data are sent from HMD, and before rendering context)"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-42",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 572.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/render/bang"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-41",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 528.0, 168.0, 20.0 ],
					"style" : "",
					"text" : "texture captured by jit.gl.node"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-40",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 528.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/gl.node/texture_output"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-37",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 484.0, 126.0, 20.0 ],
					"style" : "",
					"text" : "openGL texture name"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-36",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 484.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/camera"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-35",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 396.0, 94.0, 20.0 ],
					"style" : "",
					"text" : "angular velocity"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-22",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 418.0, 51.0, 20.0 ],
					"style" : "",
					"text" : "velocity"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-107",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 462.0, 396.0, 33.0, 20.0 ],
					"style" : "",
					"text" : "Roll"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-106",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 417.0, 396.0, 33.0, 20.0 ],
					"style" : "",
					"text" : "Yaw"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-87",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 374.0, 396.0, 41.0, 20.0 ],
					"style" : "",
					"text" : "Pitch"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-72",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 374.0, 418.0, 18.0, 20.0 ],
					"style" : "",
					"text" : "Z"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-85",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 352.0, 418.0, 18.0, 20.0 ],
					"style" : "",
					"text" : "Y"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-86",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 330.0, 418.0, 18.0, 20.0 ],
					"style" : "",
					"text" : "X"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-129",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 374.0, 225.0, 20.0 ],
					"style" : "",
					"text" : "Squeeze (Commit: 0.25, Uncommit: 0.2)"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-70",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 330.0, 330.0, 33.0, 20.0 ],
					"style" : "",
					"text" : "Grip"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-69",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 330.0, 65.0, 20.0 ],
					"style" : "",
					"text" : "App Menu"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-128",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 462.0, 352.0, 56.0, 20.0 ],
					"style" : "",
					"text" : "Pressed"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-127",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 396.0, 352.0, 49.0, 20.0 ],
					"style" : "",
					"text" : "Y 1./-1."
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-126",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 330.0, 352.0, 49.0, 20.0 ],
					"style" : "",
					"text" : "X -1./1."
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-125",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 352.0, 59.0, 20.0 ],
					"style" : "",
					"text" : "Touched"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-29",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 308.0, 335.0, 20.0 ],
					"style" : "",
					"text" : "position in meters relative to the virtual-world navigation pose"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-30",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 286.0, 365.0, 20.0 ],
					"style" : "",
					"text" : "orientation (quaternion) relative to the virtual-world navigation pose"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-31",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 264.0, 323.0, 20.0 ],
					"style" : "",
					"text" : "position in meters relative to the real-world tracking system"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-32",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 242.0, 135.0, 20.0 ],
					"style" : "",
					"text" : "orientation (quaternion)"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-24",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 132.0, 335.0, 20.0 ],
					"style" : "",
					"text" : "position in meters relative to the virtual-world navigation pose"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-25",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 110.0, 365.0, 20.0 ],
					"style" : "",
					"text" : "orientation (quaternion) relative to the virtual-world navigation pose"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-26",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 88.0, 323.0, 20.0 ],
					"style" : "",
					"text" : "position in meters relative to the real-world tracking system"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-28",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 264.0, 66.0, 135.0, 20.0 ],
					"style" : "",
					"text" : "orientation (quaternion)"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-23",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 132.0, 180.0, 20.0 ],
					"style" : "",
					"text" : "/vr/HMD/position"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-21",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 110.0, 180.0, 20.0 ],
					"style" : "",
					"text" : "/vr/HMD/quat"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-19",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 88.0, 180.0, 20.0 ],
					"style" : "",
					"text" : "/vr/HMD/tracked_position"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-17",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 66.0, 180.0, 20.0 ],
					"style" : "",
					"text" : "/vr/HMD/tracked_quat"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-27",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 44.0, 220.0, 147.5, 20.0 ],
					"style" : "",
					"text" : "RIGHT CONTROLLER"
				}

			}
, 			{
				"box" : 				{
					"id" : "obj-15",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 44.0, 44.0, 68.0, 20.0 ],
					"style" : "",
					"text" : "HEADSET"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-14",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 418.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/velocity"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-13",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 396.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/angular_velocity"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-12",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 374.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/trigger"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-11",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 352.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/trackpad"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-10",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 330.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/buttons"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-8",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 308.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/position"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-9",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 286.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/quat"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-7",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 264.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/tracked_position"
				}

			}
, 			{
				"box" : 				{
					"bgcolor" : [ 0.871412, 0.955014, 0.629622, 1.0 ],
					"id" : "obj-6",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 66.0, 242.0, 177.0, 20.0 ],
					"style" : "",
					"text" : "/vr/controller/1/tracked_quat"
				}

			}
 ],
		"lines" : [ 			{
				"patchline" : 				{
					"destination" : [ "obj-18", 1 ],
					"disabled" : 0,
					"hidden" : 0,
					"midpoints" : [ 775.5, 122.0, 866.5, 122.0 ],
					"source" : [ "obj-1", 0 ]
				}

			}
, 			{
				"patchline" : 				{
					"destination" : [ "obj-4", 0 ],
					"disabled" : 0,
					"hidden" : 0,
					"source" : [ "obj-5", 0 ]
				}

			}
, 			{
				"patchline" : 				{
					"destination" : [ "obj-66", 0 ],
					"disabled" : 0,
					"hidden" : 0,
					"source" : [ "obj-65", 0 ]
				}

			}
 ],
		"styles" : [ 			{
				"name" : "Jamoma_highlighted_orange",
				"default" : 				{
					"accentcolor" : [ 1.0, 0.5, 0.0, 1.0 ]
				}
,
				"parentstyle" : "",
				"multi" : 0
			}
 ]
	}

}
