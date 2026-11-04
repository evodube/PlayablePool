#pragma once

namespace fp::CameraHook
{
	// Hooks FreeCameraState::Update and ::GetTranslation (see CameraHook.cpp). Safe to call more than once.
	void Install();
}
