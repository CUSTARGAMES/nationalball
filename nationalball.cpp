// nationalball.cpp - Nationalball Open Source Football Game
// GPL v3 License - Uses OpenGL + GLFW

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
#define BALL_RADIUS 0.3f
#define MAX_POWER 20.0f
#define NET_ROWS 10
#define NET_COLS 8
#define NET_WIDTH 5.0f
#define NET_HEIGHT 3.0f
#define GOAL_Z -12.0f
#define ARENA_SIZE 20.0f

// ============ MATH HELPERS ============
struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
};

Vector3 operator+(const Vector3& a, const Vector3& b) {
    return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
}
Vector3 operator-(const Vector3& a, const Vector3& b) {
    return Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
}
Vector3 operator*(const Vector3& v, float s) {
    return Vector3(v.x * s, v.y * s, v.z * s);
}
float length(const Vector3& v) {
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}
Vector3 normalize(const Vector3& v) {
    float len = length(v);
    if (len < 0.0001f) return Vector3(0, 0, 0);
    return Vector3(v.x/len, v.y/len, v.z/len);
}

// ============ STRUCTURES ============
struct Ball {
    Vector3 position;
    Vector3 velocity;
    float radius;
    bool isGrounded;
    bool isScored;
    float friction;
    float bounce;
};

struct NetPoint {
    Vector3 position;
    Vector3 restPosition;
    Vector3 velocity;
    bool isFixed;
};

struct Net {
    NetPoint points[NET_ROWS][NET_COLS];
    float springConstant;
    float damping;
    bool ballCaught;
};

struct GameState {
    int score;
    int attempts;
    bool isGoal;
    float goalTimer;
    float power;
    bool isCharging;
};

// ============ GLOBAL VARIABLES ============
Ball ball;
Net net;
GameState state;
GLuint shaderProgram;
GLuint vao, vbo;

// ============ SHADER SOURCES ============
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
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

// ============ SHADER COMPILATION ============
GLuint CompileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "Shader compilation failed: " << infoLog << std::endl;
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
        std::cerr << "Program linking failed: " << infoLog << std::endl;
    }
    
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

// ============ BALL FUNCTIONS ============
void InitBall() {
    ball.position = Vector3(0, BALL_RADIUS, 0);
    ball.velocity = Vector3(0, 0, 0);
    ball.radius = BALL_RADIUS;
    ball.isGrounded = true;
    ball.isScored = false;
    ball.friction = 0.985f;
    ball.bounce = 0.6f;
}

void UpdateBall(float dt) {
    if (ball.isScored) return;
    
    if (!ball.isGrounded) {
        ball.velocity.y -= 9.8f * dt;
    }
    
    ball.velocity.x *= powf(ball.friction, dt * 10);
    ball.velocity.z *= powf(ball.friction, dt * 10);
    
    ball.position.x += ball.velocity.x * dt;
    ball.position.y += ball.velocity.y * dt;
    ball.position.z += ball.velocity.z * dt;
    
    if (ball.position.y < ball.radius) {
        ball.position.y = ball.radius;
        ball.velocity.y *= -ball.bounce;
        ball.isGrounded = true;
        if (fabs(ball.velocity.y) < 0.1f) ball.velocity.y = 0;
    } else {
        ball.isGrounded = false;
    }
    
    if (fabs(ball.position.x) > ARENA_SIZE) {
        ball.position.x = (ball.position.x > 0) ? ARENA_SIZE : -ARENA_SIZE;
        ball.velocity.x *= -ball.bounce;
    }
    if (fabs(ball.position.z) > ARENA_SIZE) {
        ball.position.z = (ball.position.z > 0) ? ARENA_SIZE : -ARENA_SIZE;
        ball.velocity.z *= -ball.bounce;
    }
    
    if (length(ball.velocity) < 0.01f) {
        ball.velocity = Vector3(0, 0, 0);
    }
}

void ShootBall(Vector3 direction, float power) {
    if (ball.isScored) return;
    ball.velocity.x = direction.x * power;
    ball.velocity.y = direction.y * power + 2.0f;
    ball.velocity.z = direction.z * power;
    ball.isGrounded = false;
}

void ResetBall() {
    ball.position = Vector3(0, BALL_RADIUS, 0);
    ball.velocity = Vector3(0, 0, 0);
    ball.isGrounded = true;
    ball.isScored = false;
}

// ============ NET FUNCTIONS ============
void InitNet() {
    net.springConstant = 60.0f;
    net.damping = 0.85f;
    net.ballCaught = false;
    
    Vector3 center(0, 1.5f, GOAL_Z);
    for (int row = 0; row < NET_ROWS; row++) {
        for (int col = 0; col < NET_COLS; col++) {
            float x = (float)col / (NET_COLS - 1) * NET_WIDTH - NET_WIDTH/2;
            float y = (float)row / (NET_ROWS - 1) * NET_HEIGHT;
            
            net.points[row][col].restPosition = Vector3(
                center.x + x,
                center.y + y,
                center.z
            );
            net.points[row][col].position = net.points[row][col].restPosition;
            net.points[row][col].velocity = Vector3(0, 0, 0);
            
            bool isEdge = (row == 0 || row == NET_ROWS-1 || 
                          col == 0 || col == NET_COLS-1);
            net.points[row][col].isFixed = isEdge;
        }
    }
}

void UpdateNet(float dt) {
    net.ballCaught = false;
    
    for (int row = 1; row < NET_ROWS-1; row++) {
        for (int col = 1; col < NET_COLS-1; col++) {
            NetPoint* p = &net.points[row][col];
            if (p->isFixed) continue;
            
            Vector3 diff = ball.position - p->position;
            float dist = length(diff);
            
            if (dist < ball.radius + 0.3f && !ball.isScored) {
                Vector3 pushDir = normalize(diff);
                float pushForce = 3.0f * (ball.radius + 0.3f - dist);
                p->position = p->position + pushDir * pushForce;
                
                ball.velocity = ball.velocity * 0.2f;
                ball.position = ball.position + pushDir * 0.05f;
                net.ballCaught = true;
            }
            
            Vector3 displacement = p->position - p->restPosition;
            Vector3 springForce = displacement * (-net.springConstant);
            Vector3 dampingForce = p->velocity * (-net.damping);
            
            Vector3 acceleration = springForce + dampingForce;
            p->velocity = p->velocity + acceleration * dt;
            p->position = p->position + p->velocity * dt;
            
            float maxDist = 1.0f;
            if (length(displacement) > maxDist) {
                p->position = p->restPosition + normalize(displacement) * maxDist;
            }
        }
    }
}

bool CheckGoal() {
    if (ball.isScored) return false;
    
    Vector3 netCenter = net.points[NET_ROWS/2][NET_COLS/2].restPosition;
    
    if (ball.position.z < netCenter.z - 0.5f) {
        if (fabs(ball.position.x) < NET_WIDTH/2 - 0.3f) {
            if (ball.position.y < NET_HEIGHT - 0.3f) {
                return true;
            }
        }
    }
    return false;
}

// ============ DRAWING FUNCTIONS ============
void DrawSphere(Vector3 pos, float radius, Vector3 color) {
    glUseProgram(shaderProgram);
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(pos.x, pos.y, pos.z));
    model = glm::scale(model, glm::vec3(radius, radius, radius));
    
    GLuint modelLoc = glGetUniformLocation(shaderProgram, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniform3f(glGetUniformLocation(shaderProgram, "color"), color.x, color.y, color.z);
    
    // Draw a sphere using icosahedron approximation
    // For simplicity, we'll draw a cube (you can add sphere mesh later)
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void DrawLine(Vector3 start, Vector3 end, Vector3 color) {
    // Simple line drawing with GL_LINES
    // We'll implement this later
}

void DrawUI(float power) {
    // UI will be drawn with OpenGL overlays or a library like ImGui
}

// ============ CAMERA ============
glm::mat4 GetViewMatrix() {
    glm::vec3 cameraPos(12.0f, 8.0f, 8.0f);
    glm::vec3 cameraTarget(0, 1.0f, -3.0f);
    glm::vec3 up(0, 1.0f, 0);
    return glm::lookAt(cameraPos, cameraTarget, up);
}

glm::mat4 GetProjectionMatrix() {
    return glm::perspective(glm::radians(50.0f), 
                            (float)SCREEN_WIDTH / SCREEN_HEIGHT, 
                            0.1f, 100.0f);
}

// ============ KEYBOARD/MOUSE ============
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        ResetBall();
        state.isGoal = false;
    }
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            // Get mouse position and check if clicking on ball
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            
            // Simple click detection (you can implement proper ray picking)
            state.isCharging = true;
            state.power = 0;
        } else if (action == GLFW_RELEASE && state.isCharging) {
            if (state.power > 1.0f) {
                Vector3 goalCenter(0, 1.5f, GOAL_Z - 1.0f);
                Vector3 direction = normalize(goalCenter - ball.position);
                direction.x += (rand() % 100 - 50) / 1000.0f;
                direction.y += (rand() % 100 - 50) / 1000.0f;
                direction = normalize(direction);
                
                ShootBall(direction, state.power);
                state.attempts++;
            }
            state.isCharging = false;
            state.power = 0;
        }
    }
}

// ============ MAIN ============
int main() {
    srand(time(NULL));
    
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, 
                                          "Nationalball - OpenGL", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }
    
    // Create shader program
    shaderProgram = CreateShaderProgram();
    
    // Create a simple cube VAO (for testing)
    float vertices[] = {
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
        0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
        // Add more faces...
    };
    // Simplified: just set up basic VAO
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Initialize game
    InitBall();
    InitNet();
    state.score = 0;
    state.attempts = 0;
    state.isGoal = false;
    state.goalTimer = 0;
    state.power = 0;
    state.isCharging = false;
    
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.12f, 0.16f, 1.0f);
    
    // Main loop
    while (!glfwWindowShouldClose(window)) {
        float dt = 0.016f; // Fixed timestep for simplicity
        
        // Update
        UpdateBall(dt);
        UpdateNet(dt);
        
        if (state.isCharging) {
            state.power += dt * 15.0f;
            if (state.power > MAX_POWER) state.power = MAX_POWER;
        }
        
        if (CheckGoal() && !state.isGoal) {
            state.score++;
            state.isGoal = true;
            state.goalTimer = 2.0f;
            ball.isScored = true;
        }
        
        if (state.isGoal) {
            state.goalTimer -= dt;
            if (state.goalTimer <= 0) {
                state.isGoal = false;
                ResetBall();
            }
        }
        
        // Render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Set view and projection matrices
        glm::mat4 view = GetViewMatrix();
        glm::mat4 projection = GetProjectionMatrix();
        
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 
                           1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 
                           1, GL_FALSE, glm::value_ptr(projection));
        
        // Draw ball
        DrawSphere(ball.position, ball.radius, 
                   ball.isScored ? Vector3(1, 0.84f, 0) : Vector3(1, 0.5f, 0));
        
        // Draw net (placeholder - you'll need proper drawing)
        for (int row = 0; row < NET_ROWS; row++) {
            for (int col = 0; col < NET_COLS; col++) {
                // Draw net points as small spheres
                NetPoint p = net.points[row][col];
                Vector3 color = (row == 0 || row == NET_ROWS-1 || 
                                col == 0 || col == NET_COLS-1) ? 
                                Vector3(1, 0, 0) : Vector3(1, 1, 1);
                DrawSphere(p.position, 0.05f, color);
            }
        }
        
        // Draw UI
        std::cout << "Score: " << state.score << " | Attempts: " << state.attempts;
        if (state.isCharging) {
            std::cout << " | Power: " << (int)(state.power/MAX_POWER * 100) << "%";
        }
        if (state.isGoal) {
            std::cout << " | GOAL!";
        }
        std::cout << std::endl;
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // Cleanup
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(shaderProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
