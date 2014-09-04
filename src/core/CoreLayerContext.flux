interface {
	name    ILayerContext
	version 1.0
	object  CoreLayerContext

	method {
		name GetPrimaryRegion

		arg {
			name      create
			direction input
			type      enum
			typename  DFBBoolean
		}

		arg {
			name      region
			direction output
			type      object
			typename  CoreLayerRegion
		}
	}

	method {
		name TestConfiguration

		arg {
			name      config
			direction input
			type      struct
			typename  DFBDisplayLayerConfig
		}

		arg {
			name      failed
			direction output
			type      enum
			typename  DFBDisplayLayerConfigFlags
			optional  yes
		}
	}

	method {
		name SetConfiguration

		arg {
			name      config
			direction input
			type      struct
			typename  DFBDisplayLayerConfig
		}
	}

	method {
		name SetSrcColorKey

		arg {
			name      key
			direction input
			type      struct
			typename  DFBColorKey
		}
	}

	method {
		name SetDstColorKey

		arg {
			name      key
			direction input
			type      struct
			typename  DFBColorKey
		}
	}

	method {
		name SetSourceRectangle

		arg {
			name      rectangle
			direction input
			type      struct
			typename  DFBRectangle
		}
	}

	method {
		name SetScreenLocation

		arg {
			name      location
			direction input
			type      struct
			typename  DFBLocation
		}
	}

	method {
		name SetScreenRectangle

		arg {
			name      rectangle
			direction input
			type      struct
			typename  DFBRectangle
		}
	}

	method {
		name SetScreenPosition

		arg {
			name      position
			direction input
			type      struct
			typename  DFBPoint
		}
	}

	method {
		name SetOpacity

		arg {
			name      opacity
			direction input
			type      int
			typename  u8
		}
	}

	method {
		name SetRotation

		arg {
			name      rotation
			direction input
			type      int
			typename  s32
		}
	}

	method {
		name SetColorAdjustment

		arg {
			name      adjustment
			direction input
			type      struct
			typename  DFBColorAdjustment
		}
	}

	method {
		name SetStereoDepth

		arg {
			name      follow_video
			direction input
			type      enum
			typename  DFBBoolean
		}

		arg {
			name      z
			direction input
			type      int
			typename  s32
		}
	}

	method {
		name SetFieldParity

		arg {
			name      field
			direction input
			type      int
			typename  u32
		}
	}

	method {
		name SetClipRegions

		arg {
			name      regions
			direction input
			type      struct
			typename  DFBRegion
			count     num 
		}

		arg {
			name      num
			direction input
			type      int
			typename  u32
		}

		arg {
			name      positive
			direction input
			type      enum
			typename  DFBBoolean
		}
	}

	method {
		name CreateWindow

		arg {
			name      description
			direction input
			type      struct
			typename  DFBWindowDescription
		}

		arg {
			name      window
			direction output
			type      object
			typename  CoreWindow
		}
	}

	method {
		name FindWindow

		arg {
			name      window_id
			direction input
			type      int
			typename  u32
		}

		arg {
			name      window
			direction output
			type      object
			typename  CoreWindow
		}
	}

	method {
		name FindWindowByResourceID

		arg {
			name      resource_id
			direction input
			type      int
			typename  u64
		}

		arg {
			name      window
			direction output
			type      object
			typename  CoreWindow
		}
	}
}

