#include <napi/native_api.h>

#include <ace/xcomponent/native_interface_xcomponent.h>

#include <string>
#include <vector>

#include "loaf/Runtime.h"

#define LOG_TAG "loafcore"
#include "Log.h"

namespace {

OH_NativeXComponent *g_xcomponent = nullptr;
napi_threadsafe_function g_eventFunction = nullptr;

int32_t surfaceWidth = 0;
int32_t surfaceHeight = 0;

std::string stringArg(napi_env env, napi_value value) {
	size_t length = 0;
	napi_get_value_string_utf8(env, value, nullptr, 0, &length);
	std::string text(length, '\0');
	napi_get_value_string_utf8(env, value, text.data(), length + 1, &length);
	return text;
}

double doubleArg(napi_env env, napi_value value, double fallback = 0.0) {
	double result = fallback;
	napi_get_value_double(env, value, &result);
	return result;
}

bool boolArg(napi_env env, napi_value value, bool fallback = false) {
	bool result = fallback;
	napi_get_value_bool(env, value, &result);
	return result;
}

//--------------------------------------------------------------
void callJs(napi_env env, napi_value jsCallback, void *context, void *data) {
	if(env == nullptr || jsCallback == nullptr) {
		return;
	}
	std::string *json = static_cast<std::string *>(data);
	napi_value undefined = nullptr;
	napi_get_undefined(env, &undefined);
	napi_value message = nullptr;
	napi_create_string_utf8(env, json->c_str(), json->size(), &message);
	napi_value result = nullptr;
	napi_call_function(env, undefined, jsCallback, 1, &message, &result);
	delete json;
}

//--------------------------------------------------------------
void sendEvent(const std::string &json) {
	if(g_eventFunction == nullptr) {
		return;
	}
	auto *payload = new std::string(json);
	napi_call_threadsafe_function(g_eventFunction, payload, napi_tsfn_nonblocking);
}

//--------------------------------------------------------------
void onSurfaceCreated(OH_NativeXComponent *component, void *window) {
	uint64_t width = 0;
	uint64_t height = 0;
	OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
	surfaceWidth = (int32_t)width;
	surfaceHeight = (int32_t)height;
	loaf::Runtime::instance().surfaceCreated(window, (int)width, (int)height);
}

void onSurfaceChanged(OH_NativeXComponent *component, void *window) {
	uint64_t width = 0;
	uint64_t height = 0;
	OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
	surfaceWidth = (int32_t)width;
	surfaceHeight = (int32_t)height;
	loaf::Runtime::instance().surfaceChanged(window, (int)width, (int)height);
}

void onSurfaceDestroyed(OH_NativeXComponent *component, void *window) {
	loaf::Runtime::instance().surfaceDestroyed();
}

void dispatchTouchEvent(OH_NativeXComponent *component, void *window) {
	OH_NativeXComponent_TouchEvent touchEvent;
	if(OH_NativeXComponent_GetTouchEvent(component, window, &touchEvent) != 0) {
		return;
	}
	for(uint32_t i = 0; i < touchEvent.numPoints; ++i) {
		const OH_NativeXComponent_TouchPoint &point = touchEvent.touchPoints[i];
		int action = 0;
		if(point.type == OH_NATIVEXCOMPONENT_DOWN) {
			action = 1;
		}
		else if(point.type == OH_NATIVEXCOMPONENT_UP) {
			action = 2;
		}
		else if(point.type == OH_NATIVEXCOMPONENT_CANCEL) {
			action = 2;
		}
		if(point.type != OH_NATIVEXCOMPONENT_UNKNOWN) {
			loaf::Runtime::instance().pointerEventPixels(point.x, point.y, 0, action);
		}
	}
}

void dispatchKeyEvent(OH_NativeXComponent *component, void *window) {
	OH_NativeXComponent_KeyEvent *keyEvent = nullptr;
	if(OH_NativeXComponent_GetKeyEvent(component, &keyEvent) != 0 || keyEvent == nullptr) {
		return;
	}
	OH_NativeXComponent_KeyCode code = KEY_UNKNOWN;
	OH_NativeXComponent_KeyAction action = OH_NATIVEXCOMPONENT_KEY_ACTION_UNKNOWN;
	OH_NativeXComponent_GetKeyEventCode(keyEvent, &code);
	OH_NativeXComponent_GetKeyEventAction(keyEvent, &action);
	if(action == OH_NATIVEXCOMPONENT_KEY_ACTION_DOWN) {
		loaf::Runtime::instance().keyEvent((int)code, true);
	}
	else if(action == OH_NATIVEXCOMPONENT_KEY_ACTION_UP) {
		loaf::Runtime::instance().keyEvent((int)code, false);
	}
}

OH_NativeXComponent_Callback g_callbacks = {
	.OnSurfaceCreated = onSurfaceCreated,
	.OnSurfaceChanged = onSurfaceChanged,
	.OnSurfaceDestroyed = onSurfaceDestroyed,
	.DispatchTouchEvent = dispatchTouchEvent,
};

//--------------------------------------------------------------
napi_value jsSetDensity(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 1) {
		loaf::Runtime::instance().setDensity((float)doubleArg(env, args[0], 2.0));
	}
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsOnEvent(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc < 1) {
		return nullptr;
	}
	napi_valuetype type = napi_undefined;
	napi_typeof(env, args[0], &type);
	if(type != napi_function) {
		return nullptr;
	}
	if(g_eventFunction != nullptr) {
		napi_release_threadsafe_function(g_eventFunction, napi_tsfn_release);
		g_eventFunction = nullptr;
	}
	napi_value name = nullptr;
	napi_create_string_utf8(env, "loafEvent", NAPI_AUTO_LENGTH, &name);
	napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr,
	                                nullptr, callJs, &g_eventFunction);
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsLoadScript(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 1) {
		loaf::Runtime::instance().loadScript(stringArg(env, args[0]));
	}
	napi_value result = nullptr;
	napi_get_boolean(env, true, &result);
	return result;
}

//--------------------------------------------------------------
napi_value jsLoadScriptText(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3] = {nullptr, nullptr, nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 3) {
		loaf::Runtime::instance().loadScriptText(stringArg(env, args[0]), stringArg(env, args[1]),
		                                         stringArg(env, args[2]));
	}
	napi_value result = nullptr;
	napi_get_boolean(env, true, &result);
	return result;
}

//--------------------------------------------------------------
napi_value jsReload(napi_env env, napi_callback_info info) {
	loaf::Runtime::instance().reloadScript();
	napi_value result = nullptr;
	napi_get_boolean(env, true, &result);
	return result;
}

//--------------------------------------------------------------
napi_value jsStop(napi_env env, napi_callback_info info) {
	loaf::Runtime::instance().clearScript();
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsEvaluate(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 1) {
		loaf::Runtime::instance().evaluate(stringArg(env, args[0]));
	}
	napi_value result = nullptr;
	napi_get_boolean(env, true, &result);
	return result;
}

//--------------------------------------------------------------
napi_value jsSetListenPort(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 1) {
		loaf::Runtime::instance().setOscListenPort((int)doubleArg(env, args[0], 9990.0));
	}
	napi_value result = nullptr;
	napi_get_boolean(env, true, &result);
	return result;
}

//--------------------------------------------------------------
napi_value jsStartListening(napi_env env, napi_callback_info info) {
	loaf::Runtime::instance().startOsc();
	napi_value result = nullptr;
	napi_get_boolean(env, true, &result);
	return result;
}

//--------------------------------------------------------------
napi_value jsStopListening(napi_env env, napi_callback_info info) {
	loaf::Runtime::instance().stopOsc();
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsSetSendHost(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 1) {
		loaf::Runtime::instance().setOscSendHost(stringArg(env, args[0]));
	}
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsSetSendPort(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 1) {
		loaf::Runtime::instance().setOscSendPort((int)doubleArg(env, args[0], 8880.0));
	}
	napi_value result = nullptr;
	napi_get_boolean(env, true, &result);
	return result;
}

//--------------------------------------------------------------
napi_value jsSendOsc(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3] = {nullptr, nullptr, nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 3) {
		loaf::Runtime::instance().sendOsc(stringArg(env, args[0]), stringArg(env, args[1]),
		                                  stringArg(env, args[2]));
	}
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsSetVerbose(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	loaf::env().verbose = argc >= 1 && boolArg(env, args[0]);
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsGetInfo(napi_env env, napi_callback_info info) {
	std::string json = loaf::Runtime::instance().infoJson();
	napi_value result = nullptr;
	napi_create_string_utf8(env, json.c_str(), json.size(), &result);
	return result;
}

//--------------------------------------------------------------
napi_value jsPointerEvent(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4] = {nullptr, nullptr, nullptr, nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 4) {
		loaf::Runtime::instance().pointerEvent((float)doubleArg(env, args[0]),
		                                       (float)doubleArg(env, args[1]),
		                                       (int)doubleArg(env, args[2], 0.0),
		                                       (int)doubleArg(env, args[3], 0.0));
	}
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsKeyEvent(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2] = {nullptr, nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	if(argc >= 2) {
		loaf::Runtime::instance().keyEvent((int)doubleArg(env, args[0]),
		                                   boolArg(env, args[1]));
	}
	return nullptr;
}

//--------------------------------------------------------------
napi_value jsDragEvent(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
	std::vector<std::string> files;
	if(argc >= 1) {
		bool isArray = false;
		napi_is_array(env, args[0], &isArray);
		if(isArray) {
			uint32_t length = 0;
			napi_get_array_length(env, args[0], &length);
			for(uint32_t i = 0; i < length; ++i) {
				napi_value element = nullptr;
				napi_get_element(env, args[0], i, &element);
				files.push_back(stringArg(env, element));
			}
		}
	}
	loaf::Runtime::instance().dragEvent(files);
	return nullptr;
}

//--------------------------------------------------------------
napi_value Init(napi_env env, napi_value exports) {
	napi_value exportInstance = nullptr;
	if(napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) == napi_ok) {
		napi_unwrap(env, exportInstance, reinterpret_cast<void **>(&g_xcomponent));
	}
	if(g_xcomponent != nullptr) {
		OH_NativeXComponent_RegisterCallback(g_xcomponent, &g_callbacks);
		LOGI("native XComponent registered");
	}

	napi_property_descriptor properties[] = {
		{"loadScript", nullptr, jsLoadScript, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"loadScriptText", nullptr, jsLoadScriptText, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"reload", nullptr, jsReload, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"stop", nullptr, jsStop, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"evaluate", nullptr, jsEvaluate, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setListenPort", nullptr, jsSetListenPort, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"startListening", nullptr, jsStartListening, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"stopListening", nullptr, jsStopListening, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setSendHost", nullptr, jsSetSendHost, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setSendPort", nullptr, jsSetSendPort, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"sendOsc", nullptr, jsSendOsc, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setVerbose", nullptr, jsSetVerbose, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"getInfo", nullptr, jsGetInfo, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"onEvent", nullptr, jsOnEvent, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setDensity", nullptr, jsSetDensity, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"pointerEvent", nullptr, jsPointerEvent, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"keyEvent", nullptr, jsKeyEvent, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"dragEvent", nullptr, jsDragEvent, nullptr, nullptr, nullptr, napi_default, nullptr},
	};
	napi_define_properties(env, exports, sizeof(properties) / sizeof(properties[0]), properties);

	loaf::Runtime::instance().setEventSink(sendEvent);
	return exports;
}

} // namespace

static napi_module loafModule = {
	.nm_version = 1,
	.nm_flags = 0,
	.nm_filename = nullptr,
	.nm_register_func = Init,
	.nm_modname = "loafcore",
	.nm_priv = nullptr,
	.reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterLoafModule(void) {
	napi_module_register(&loafModule);
}
