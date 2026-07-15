// game.cpp - Nationalball 3D Model Viewer
// GPL v3 License - Pure OpenGL + GLFW + Assimp

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <string>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

// ============ SIMPLE MATH ============
struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct Mat4 {
    float m[16];
    Mat4() {
        for(int i=0; i<16; i++) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
};

Mat4 perspective(float fov, float aspect, float near, float far) {
    Mat4 result;
    float tanHalfFov = tanf(fov * 0.5f);
    result.m[0] = 1.0f / (aspect * tanHalfFov);
    result.m[5] = 1.0f / tanHalfFov;
    result.m[10] = -(far + near) / (far - near);
    result.m[11] = -1.0f;
    result.m[14] = -(2.0f * far * near) / (far - near);
    result.m[15] = 0.0f;
    return result;
}

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 z = normalize(eye - target);
    Vec3 x = normalize(up ^ z);
    Vec3 y = z ^ x;
    Mat4 result;
    result.m[0] = x.x; result.m[1] = x.y; result.m[2] = x.z; result.m[3] = -(x * eye);
    result.m[4] = y.x; result.m[5] = y.y; result.m[6] = y.z; result.m[7] = -(y * eye);
    result.m[8] = z.x; result.m[9] = z.y; result.m[10] = z.z; result.m[11] = -(z * eye);
    result.m[12] = 0; result.m[13] = 0; result.m[14] = 0; result.m[15] = 1;
    return result;
}

Mat4 translate(const Vec3& v) {
    Mat4 result;
    result.m[3] = v.x;
    result.m[7] = v.y;
    result.m[11] = v.z;
    return result;
}

Mat4 scale(const Vec3& v) {
    Mat4 result;
    result.m[0] = v.x;
    result.m[5] = v.y;
    result.m[10] = v.z;
    return result;
}

Mat4 rotateY(float angle) {
    Mat4 result;
    float c = cosf(angle);
    float s = sinf(angle);
    result.m[0] = c;
    result.m[2] = s;
    result.m[8] = -s;
    result.m[10] = c;
    return result;
}

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for(int i=0; i<4; i++) {
        for(int j=0; j<4; j++) {
            result.m[i*4+j] = 0;
            for(int k=0; k<4; k++) {
                result.m[i*4+j] += a.m[i*4+k] * b.m[k*4+j];
            }
        }
    }
    return result;
}

Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x+b.x, a.y+b.y, a.z+b.z); }
Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x-b.x, a.y-b.y, a.z-b.z); }
Vec3 operator*(const Vec3& v, float s) { return Vec3(v.x*s, v.y*s, v.z*s); }
Vec3 operator*(float s, const Vec3& v) { return Vec3(v.x*s, v.y*s, v.z*s); }
float length(const Vec3& v) { return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); }
Vec3 normalize(const Vec3& v) {
    float len = length(v);
    if (len < 0.0001f) return Vec3(0, 0, 0);
    return Vec3(v.x/len, v.y/len, v.z/len);
}
Vec3 operator^(const Vec3& a, const Vec3& b) {
    return Vec3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}
float dot(const Vec3& a, const Vec3& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

// ============ SHADER ============
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
uniform vec3 color;
out vec4 FragColor;
void main() {
    FragColor = vec4(color, 1.0);
}
)";

GLuint CompileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "Shader error: " << infoLog << std::endl;
    }
    return shader;
}

GLuint CreateShaderProgram() {
    GLuint vertex = CompileShader(vertexShaderSource, GL_VERTEX_SHADER);
    GLuint fragment = CompileShader(fragmentShaderSource, GL_FRAGMENT_SHADER);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cerr << "Program error: " << infoLog << std::endl;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

// ============ MESH STRUCTURE ============
struct Mesh {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    GLuint VAO, VBO, EBO;
    Vec3 color;
    
    void LoadFromAssimp(aiMesh* mesh) {
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            vertices.push_back(mesh->mVertices[i].x);
            vertices.push_back(mesh->mVertices[i].y);
            vertices.push_back(mesh->mVertices[i].z);
        }
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);
            }
        }
        SetupGL();
    }
    
    void SetupGL() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
        
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
        
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    
    void Draw(GLuint shader, Mat4 model, Mat4 view, Mat4 projection) {
        glUseProgram(shader);
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, model.m);
        glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, view.m);
        glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, projection.m);
        glUniform3f(glGetUniformLocation(shader, "color"), color.x, color.y, color.z);
        
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
};

// ============ MODEL LOADER ============
std::vector<Mesh> LoadModel(const std::string& path) {
    std::vector<Mesh> meshes;
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs);
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Failed to load: " << path << " - " << importer.GetErrorString() << std::endl;
        return meshes;
    }
    
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        Mesh m;
        m.color = Vec3(1, 1, 1); // Default white
        m.LoadFromAssimp(mesh);
        meshes.push_back(m);
        std::cout << "Loaded mesh " << i << " with " << mesh->mNumVertices << " vertices" << std::endl;
    }
    return meshes;
}

// ============ KEYBOARD/MOUSE ============
Vec3 cameraPos(12.0f, 6.0f, 12.0f);
Vec3 cameraTarget(0, 0, 0);
Vec3 up(0, 1.0f, 0);
float yaw = -45.0f;
float pitch = -20.0f;
float distance = 15.0f;
bool firstMouse = true;
double lastX, lastY;

void MouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;
    
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        yaw += xoffset * 0.1f;
        pitch += yoffset * 0.1f;
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }
}

void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    distance -= yoffset * 0.5f;
    if (distance < 2.0f) distance = 2.0f;
    if (distance > 50.0f) distance = 50.0f;
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    // WASD to move target
    float speed = 0.2f;
    if (key == GLFW_KEY_W && action == GLFW_PRESS) cameraTarget.z -= speed;
    if (key == GLFW_KEY_S && action == GLFW_PRESS) cameraTarget.z += speed;
    if (key == GLFW_KEY_A && action == GLFW_PRESS) cameraTarget.x -= speed;
    if (key == GLFW_KEY_D && action == GLFW_PRESS) cameraTarget.x += speed;
}

// ============ MAIN ============
int main(int argc, char** argv) {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, 
                                          "Nationalball - 3D Viewer", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCursorPosCallback(window, MouseCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }
    
    // Create shader
    GLuint shader = CreateShaderProgram();
    
    // Load models
    std::vector<Mesh> playerMeshes = LoadModel("player.glb");
    std::vector<Mesh> ballMeshes = LoadModel("ball.glb");
    
    // If no models found, create placeholder
    if (playerMeshes.empty()) {
        std::cout << "No player.glb found - creating placeholder" << std::endl;
        Mesh placeholder;
        float cubeVerts[] = {
            -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,
             0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
            -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
             0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f,
            -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
            -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
             0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f,
             0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
            -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,
             0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f,
            -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
             0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f
        };
        for (float v : cubeVerts) placeholder.vertices.push_back(v);
        for (int i = 0; i < 36; i++) placeholder.indices.push_back(i);
        placeholder.color = Vec3(0, 1, 0);
        placeholder.SetupGL();
        playerMeshes.push_back(placeholder);
    }
    if (ballMeshes.empty()) {
        Mesh placeholder;
        // Simple sphere (20x20)
        for (int i = 0; i <= 20; i++) {
            float V = (float)i / 20.0f;
            float phi = V * 3.14159f;
            for (int j = 0; j <= 20; j++) {
                float U = (float)j / 20.0f;
                float theta = U * 2.0f * 3.14159f;
                placeholder.vertices.push_back(sinf(phi) * cosf(theta));
                placeholder.vertices.push_back(cosf(phi));
                placeholder.vertices.push_back(sinf(phi) * sinf(theta));
            }
        }
        for (int i = 0; i < 20; i++) {
            for (int j = 0; j < 20; j++) {
                int first = i * 21 + j;
                int second = first + 21;
                placeholder.indices.push_back(first);
                placeholder.indices.push_back(second);
                placeholder.indices.push_back(first + 1);
                placeholder.indices.push_back(second);
                placeholder.indices.push_back(second + 1);
                placeholder.indices.push_back(first + 1);
            }
        }
        placeholder.color = Vec3(1, 0.5f, 0);
        placeholder.SetupGL();
        ballMeshes.push_back(placeholder);
    }
    
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.15f, 0.2f, 1.0f);
    
    // UI overlay positions
    bool showHelp = true;
    
    while (!glfwWindowShouldClose(window)) {
        // Update camera
        float radYaw = yaw * 3.14159f / 180.0f;
        float radPitch = pitch * 3.14159f / 180.0f;
        cameraPos.x = cameraTarget.x + distance * cosf(radPitch) * sinf(radYaw);
        cameraPos.y = cameraTarget.y + distance * sinf(radPitch);
        cameraPos.z = cameraTarget.z + distance * cosf(radPitch) * cosf(radYaw);
        
        Mat4 view = lookAt(cameraPos, cameraTarget, up);
        Mat4 projection = perspective(50.0f * 3.14159f / 180.0f, 
                                      (float)SCREEN_WIDTH / SCREEN_HEIGHT, 
                                      0.1f, 100.0f);
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Draw grid
        glUseProgram(shader);
        Mat4 gridModel;
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, gridModel.m);
        glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, view.m);
        glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, projection.m);
        glUniform3f(glGetUniformLocation(shader, "color"), 0.2f, 0.3f, 0.2f);
        
        // Simple grid using lines (draw as thin rectangles)
        for (int i = -20; i <= 20; i++) {
            // Draw line function using small rectangles
        }
        
        // Draw models
        for (Mesh& m : playerMeshes) {
            Mat4 model = translate(Vec3(-2, 0, 0)) * scale(Vec3(0.5f, 0.5f, 0.5f));
            m.Draw(shader, model, view, projection);
        }
        for (Mesh& m : ballMeshes) {
            Mat4 model = translate(Vec3(2, 0, 0)) * scale(Vec3(0.3f, 0.3f, 0.3f));
            m.Draw(shader, model, view, projection);
        }
        
        // Draw help text using OpenGL (simplified - just print to console)
        if (showHelp) {
            std::cout << "\r[Controls] WASD: move target | Right-click+drag: orbit | Scroll: zoom | ESC: exit    ";
            std::cout.flush();
        }
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    glDeleteProgram(shader);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
