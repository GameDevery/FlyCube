#pragma once

#include <Scene/IScene.h>
#include <GLFW/glfw3.h>
#include <functional>
#include <string>
#include <memory>
#include <ApiType/ApiType.h>

struct AppRect
{
    int width;
    int height;
};

class AppBox
{
public:
    AppBox(int argc, char* argv[], const std::string& title, ApiType api_type = ApiType::kVulkan);
    ~AppBox();
    int Run();
    bool ShouldClose();
    void PollEvents();

    AppRect GetAppRect() const;
    GLFWwindow* GetWindow() const;
    void UpdateFps();

    static AppRect GetPrimaryMonitorRect();

private:
    void Init();
    void InitWindow();
    void SetWindowToCenter();

    static void OnSizeChanged(GLFWwindow* window, int width, int height);
    static void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void OnMouse(GLFWwindow* window, double xpos, double ypos);
    static void OnMouseButton(GLFWwindow* window, int button, int action, int mods);
    static void OnScroll(GLFWwindow* window, double xoffset, double yoffset);
    static void OnInputChar(GLFWwindow* window, unsigned int ch);

    ApiType m_api_type;
    std::string m_title;
    IScene::Ptr m_sample;
    GLFWwindow* m_window;
    int m_width;
    int m_height;
    bool m_exit;
    uint32_t m_frame_number = 0;
    double m_last_time = 0;
};
