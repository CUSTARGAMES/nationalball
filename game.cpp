// game.cpp - Nationalball 3D GLB Viewer with TinyGLTF
// GPL v3 License - Pure OpenGL + GLFW + TinyGLTF

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "tiny_gltf.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
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
uniform vec3 objectColor;
out vec3 FragColor;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    FragColor = objectColor;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 FragColor;
out vec4 OutColor;
void main() {
    OutColor = vec4(FragColor, 1.0);
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
    int vertexCount;
    
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
        vertexCount = indices.size();
    }
    
    void Draw(GLuint shader, Mat4 model, Mat4 view, Mat4 projection) {
        glUseProgram(shader);
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, model.m);
        glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, view.m);
        glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, projection.m);
        glUniform3f(glGetUniformLocation(shader, "objectColor"), color.x, color.y, color.z);
        
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, vertexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
};

// ============ GLB LOADER USING TINYGLTF ============
std::vector<Mesh> LoadGLB(const std::string& path, Vec3 defaultColor) {
    std::vector<Mesh> meshes;
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    
    bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if (!ret) {
        std::cerr << "Failed to load GLB: " << path << std::endl;
        if (!err.empty()) std::cerr << "Error: " << err << std::endl;
        if (!warn.empty()) std::cerr << "Warning: " << warn << std::endl;
        return meshes;
    }
    
    std::cout << "✅ Loaded GLB: " << path << std::endl;
    std::cout << "  Meshes: " << model.meshes.size() << std::endl;
    
    for (const auto& gltfMesh : model.meshes) {
        for (const auto& primitive : gltfMesh.primitives) {
            Mesh mesh;
            mesh.color = defaultColor;
            
            // Get vertex positions
            auto posIt = primitive.attributes.find("POSITION");
            if (posIt != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor = model.accessors[posIt->second];
                const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[view.buffer];
                
                const float* data = reinterpret_cast<const float*>(
                    buffer.data.data() + view.byteOffset + accessor.byteOffset
                );
                
                for (size_t i = 0; i < accessor.count; i++) {
                    mesh.vertices.push_back(data[i * 3 + 0]);
                    mesh.vertices.push_back(data[i * 3 + 1]);
                    mesh.vertices.push_back(data[i * 3 + 2]);
                }
            }
            
            // Get indices
            if (primitive.indices >= 0) {
                const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[view.buffer];
                
                const unsigned char* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
                
                for (size_t i = 0; i < accessor.count; i++) {
                    unsigned int idx = 0;
                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        idx = reinterpret_cast<const unsigned short*>(data)[i];
                    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        idx = reinterpret_cast<const unsigned int*>(data)[i];
                    }
                    mesh.indices.push_back(idx);
                }
            }
            
            if (!mesh.vertices.empty() && !mesh.indices.empty()) {
                mesh.SetupGL();
                meshes.push_back(mesh);
                std::cout << "  Loaded mesh with " << mesh.vertices.size()/3 
                          << " vertices, " << mesh.indices.size() << " indices" << std::endl;
            }
        }
    }
    
    return meshes;
}

// ============ CREATE PLACEHOLDER MODELS ============
Mesh CreatePlaceholderCube(Vec3 color) {
    Mesh mesh;
    mesh.color = color;
    float verts[] = {
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
    for (float v : verts) mesh.vertices.push_back(v);
    for (int i = 0; i < 36; i++) mesh.indices.push_back(i);
    mesh.SetupGL();
    return mesh;
}

Mesh CreatePlaceholderSphere(Vec3 color) {
    Mesh mesh;
    mesh.color = color;
    int stacks = 20, slices = 20;
    for (int i = 0; i <= stacks; i++) {
        float V = (float)i / (float)stacks;
        float phi = V * 3.14159f;
        for (int j = 0; j <= slices; j++) {
            float U = (float)j / (float)slices;
            float theta = U * 2.0f * 3.14159f;
            mesh.vertices.push_back(sinf(phi) * cosf(theta));
            mesh.vertices.push_back(cosf(phi));
            mesh.vertices.push_back(sinf(phi) * sinf(theta));
        }
    }
    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            int first = i * (slices + 1) + j;
            int second = first + slices + 1;
            mesh.indices.push_back(first);
            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(second);
            mesh.indices.push_back(second + 1);
            mesh.indices.push_back(first + 1);
        }
    }
    mesh.SetupGL();
    return mesh;
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
bool showHelp = true;

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
    if (key == GLFW_KEY_H && action == GLFW_PRESS) {
        showHelp = !showHelp;
    }
    
    // WASD to move target
    float speed = 0.2f;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_W) cameraTarget.z -= speed;
        if (key == GLFW_KEY_S) cameraTarget.z += speed;
        if (key == GLFW_KEY_A) cameraTarget.x -= speed;
        if (key == GLFW_KEY_D) cameraTarget.x += speed;
        if (key == GLFW_KEY_Q) cameraTarget.y -= speed;
        if (key == GLFW_KEY_E) cameraTarget.y += speed;
    }
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
                                          "Nationalball - 3D GLB Viewer", NULL, NULL);
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
    
    // Load GLB models
    std::vector<Mesh> playerMeshes;
    std::vector<Mesh> ballMeshes;
    
    std::cout << "Loading player.glb..." << std::endl;
    playerMeshes = LoadGLB("player.glb", Vec3(0, 1, 0)); // Green
    if (playerMeshes.empty()) {
        std::cout << "⚠️  player.glb not found - using placeholder cube" << std::endl;
        playerMeshes.push_back(CreatePlaceholderCube(Vec3(0, 1, 0)));
    }
    
    std::cout << "Loading ball.glb..." << std::endl;
    ballMeshes = LoadGLB("ball.glb", Vec3(1, 0.5f, 0)); // Orange
    if (ballMeshes.empty()) {
        std::cout << "⚠️  ball.glb not found - using placeholder sphere" << std::endl;
        ballMeshes.push_back(CreatePlaceholderSphere(Vec3(1, 0.5f, 0)));
    }
    
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.15f, 0.2f, 1.0f);
    
    float modelRotation = 0.0f;
    
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
        
        // Auto-rotate models slowly
        modelRotation += 0.002f;
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Draw player model (at position -2, 0, 0)
        for (Mesh& m : playerMeshes) {
            Mat4 model = translate(Vec3(-2, 0, 0)) * scale(Vec3(0.5f, 0.5f, 0.5f));
            m.Draw(shader, model, view, projection);
        }
        
        // Draw ball model (at position 2, 0, 0)
        for (Mesh& m : ballMeshes) {
            Mat4 model = translate(Vec3(2, 0, 0)) * scale(Vec3(0.3f, 0.3f, 0.3f));
            m.Draw(shader, model, view, projection);
        }
        
        // Draw info overlay (simple console for now)
        if (showHelp) {
            std::cout << "\r[Controls] WASD: move | Q/E: up/down | Right-click+drag: orbit | Scroll: zoom | H: toggle help | ESC: exit    ";
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
