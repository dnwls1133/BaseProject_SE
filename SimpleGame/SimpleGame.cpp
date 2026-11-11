/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)
This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.
This program is distributed WITHOUT ANY WARRANTY.
*/
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "stdafx.h"
#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include "Profiler.h"
#include "Renderer.h"
#include "Game.h"
#include "Platform.h"

namespace
{
std::unique_ptr<Renderer> renderer;
Game game;

void Display(GLFWwindow* window)
{
    if (!renderer)
    {
        return;
    }

    Profile::BeginFrame();
    renderer->ResetDrawCallCount();
    {
        PROFILE_SCOPE(Render);
        game.Render(*renderer);
    }
    {
        PROFILE_SCOPE(Swap);
        glfwSwapBuffers(window);
    }
    Profile::Presented();
    Profile::EndFrame();
}

void Error(int code, const char* description)
{
    std::cerr << "[GLFW] " << code << ": " << (description ? description : "unknown error")
              << '\n';
}

unsigned char AsciiKey(int key, int modifiers)
{
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
    {
        return static_cast<unsigned char>('A' + key - GLFW_KEY_A);
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
    {
        return static_cast<unsigned char>('0' + key - GLFW_KEY_0);
    }

    switch (key)
    {
    case GLFW_KEY_ESCAPE:
        return 27;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        return 13;
    case GLFW_KEY_TAB:
        return 9;
    case GLFW_KEY_BACKSPACE:
        return 8;
    case GLFW_KEY_SPACE:
        return ' ';
    case GLFW_KEY_APOSTROPHE:
        return (modifiers & GLFW_MOD_SHIFT) ? '"' : '\'';
    case GLFW_KEY_COMMA:
        return (modifiers & GLFW_MOD_SHIFT) ? '<' : ',';
    case GLFW_KEY_MINUS:
        return (modifiers & GLFW_MOD_SHIFT) ? '_' : '-';
    case GLFW_KEY_PERIOD:
        return (modifiers & GLFW_MOD_SHIFT) ? '>' : '.';
    case GLFW_KEY_SLASH:
        return (modifiers & GLFW_MOD_SHIFT) ? '?' : '/';
    case GLFW_KEY_SEMICOLON:
        return (modifiers & GLFW_MOD_SHIFT) ? ':' : ';';
    case GLFW_KEY_EQUAL:
        return (modifiers & GLFW_MOD_SHIFT) ? '+' : '=';
    case GLFW_KEY_LEFT_BRACKET:
        return (modifiers & GLFW_MOD_SHIFT) ? '{' : '[';
    case GLFW_KEY_BACKSLASH:
        return (modifiers & GLFW_MOD_SHIFT) ? '|' : '\\';
    case GLFW_KEY_RIGHT_BRACKET:
        return (modifiers & GLFW_MOD_SHIFT) ? '}' : ']';
    case GLFW_KEY_GRAVE_ACCENT:
        return (modifiers & GLFW_MOD_SHIFT) ? '~' : '`';
    default:
        return 0;
    }
}

void Key(GLFWwindow*, int key, int, int action, int modifiers)
{
    if (action == GLFW_REPEAT)
    {
        return;
    }

    if (action != GLFW_PRESS && action != GLFW_RELEASE)
    {
        return;
    }

    const unsigned char mapped = AsciiKey(key, modifiers);
    if (mapped != 0)
    {
        game.Key(mapped, action == GLFW_PRESS);
    }
}

void Focus(GLFWwindow*, int focused)
{
    if (!focused)
    {
        game.ClearInput();
    }
}

void FramebufferSize(GLFWwindow* window, int, int)
{
    if (!renderer)
    {
        return;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width > 0 && height > 0)
    {
        renderer->Resize(width, height);
    }
}

void UpdateFrame(std::chrono::steady_clock::time_point now,
                 std::chrono::steady_clock::time_point& previousTick,
                 std::chrono::steady_clock::time_point& nextTickDue, bool& hasPreviousTick)
{
    Profile::Add(Profile::Metric::Ticks);
    Profile::Add(Profile::Metric::TimerLate,
                 std::max(0.0, std::chrono::duration<double, std::milli>(now - nextTickDue).count()));
    const float elapsed = std::chrono::duration<float>(now - previousTick).count();
    if (hasPreviousTick)
    {
        Profile::Add(Profile::Metric::TickInterval,
                     std::chrono::duration<double, std::milli>(now - previousTick).count());
    }
    previousTick = now;
    hasPreviousTick = true;

    const float dt = std::min(.05f, elapsed);
    if (elapsed > .05f)
    {
        Profile::Add(Profile::Metric::DtClamps);
    }
    {
        PROFILE_SCOPE(Update);
        game.Update(dt);
    }

    const auto period = std::chrono::microseconds(16667);
    nextTickDue += period;
    const auto afterUpdate = std::chrono::steady_clock::now();
    if (nextTickDue < afterUpdate - period)
    {
        nextTickDue = afterUpdate;
    }
}
} // namespace

int main()
{
    glfwSetErrorCallback(Error);
    if (!glfwInit())
    {
        std::cerr << "Cannot initialize GLFW. Check the window-system dependencies.\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "성혈의 잔향 - 마지막 이름", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        std::cerr << "Cannot create an OpenGL 3.3 window.\n";
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    glfwSetKeyCallback(window, Key);
    glfwSetWindowFocusCallback(window, Focus);
    glfwSetFramebufferSizeCallback(window, FramebufferSize);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK || !GLEW_VERSION_3_3)
    {
        std::cerr << "OpenGL 3.3 support required.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        framebufferWidth = 1280;
        framebufferHeight = 800;
    }

    std::error_code pathError;
    std::filesystem::current_path(Platform::ExecutableDirectory(), pathError);
    if (pathError)
    {
        std::cerr << "Cannot set the game content directory: " << pathError.message() << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    Profile::Initialize();
    renderer.reset(new Renderer(framebufferWidth, framebufferHeight));
    if (!renderer->IsInitialized())
    {
        std::cerr << "Renderer initialization failed.\n";
        Profile::Shutdown();
        renderer.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    if (!game.LoadFirstLevel("Data/"))
    {
        std::cerr << "Cannot load Data/world.catalog beside the executable. "
                     "See the content error above.\n";
        Profile::Shutdown();
        renderer.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    Profile::StartGameplay();
    constexpr auto period = std::chrono::microseconds(16667);
    auto previousTick = std::chrono::steady_clock::now();
    auto nextTickDue = previousTick + period;
    bool hasPreviousTick = false;
    int result = 0;
    try
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextTickDue)
            {
                UpdateFrame(now, previousTick, nextTickDue, hasPreviousTick);
                int width = 0;
                int height = 0;
                glfwGetFramebufferSize(window, &width, &height);
                if (width > 0 && height > 0)
                {
                    Display(window);
                }
                continue;
            }

            const double waitSeconds =
                std::chrono::duration<double>(nextTickDue - now).count();
            glfwWaitEventsTimeout(std::max(0.0, waitSeconds));
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Game runtime error: " << error.what() << '\n';
        result = 1;
    }

    Profile::Shutdown();
    renderer.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
