// nationalball.cpp - Nationalball Open Source Football Game
// GPL v3 License - Free to use, modify, and share

#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// ============ CONFIGURATION ============
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
#define NET_ROWS 10
#define NET_COLS 8
#define NET_WIDTH 5.0f
#define NET_HEIGHT 3.0f
#define GOAL_Z -12.0f
#define BALL_RADIUS 0.3f
#define MAX_POWER 20.0f
#define ARENA_SIZE 20.0f

// ============ STRUCTURES ============
typedef struct {
    Vector3 position;
    Vector3 velocity;
    float radius;
    float mass;
    float friction;
    float bounce;
    bool isGrounded;
    bool isScored;
} Ball;

typedef struct {
    Vector3 position;
    Vector3 restPosition;
    Vector3 velocity;
    bool isFixed;
} NetPoint;

typedef struct {
    NetPoint points[NET_ROWS][NET_COLS];
    float springConstant;
    float damping;
    bool ballCaught;
} Net;

typedef struct {
    Vector3 position;
    float power;
    bool isCharging;
    bool isClicked;
} Shooter;

typedef struct {
    int score;
    int attempts;
    float time;
    bool isGoal;
    float goalTimer;
} GameState;

// ============ HELPER FUNCTIONS (Raylib doesn't have these) ============
float Vector3Length(Vector3 v) {
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

Vector3 Vector3Normalize(Vector3 v) {
    float len = Vector3Length(v);
    if (len == 0) return (Vector3){0, 0, 0};
    return (Vector3){v.x/len, v.y/len, v.z/len};
}

Vector3 Vector3Scale(Vector3 v, float s) {
    return (Vector3){v.x*s, v.y*s, v.z*s};
}

Vector3 Vector3Add(Vector3 a, Vector3 b) {
    return (Vector3){a.x+b.x, a.y+b.y, a.z+b.z};
}

Vector3 Vector3Subtract(Vector3 a, Vector3 b) {
    return (Vector3){a.x-b.x, a.y-b.y, a.z-b.z};
}

// ============ BALL FUNCTIONS ============
Ball CreateBall(Vector3 pos) {
    Ball b;
    b.position = pos;
    b.velocity = (Vector3){0, 0, 0};
    b.radius = BALL_RADIUS;
    b.mass = 1.0f;
    b.friction = 0.985f;
    b.bounce = 0.6f;
    b.isGrounded = true;
    b.isScored = false;
    return b;
}

void UpdateBall(Ball* b, float dt) {
    if (b->isScored) return;
    
    // Gravity
    if (!b->isGrounded) {
        b->velocity.y -= 9.8f * dt;
    }
    
    // Friction
    b->velocity.x *= powf(b->friction, dt * 10);
    b->velocity.z *= powf(b->friction, dt * 10);
    
    // Update position
    b->position.x += b->velocity.x * dt;
    b->position.y += b->velocity.y * dt;
    b->position.z += b->velocity.z * dt;
    
    // Ground collision
    if (b->position.y < b->radius) {
        b->position.y = b->radius;
        b->velocity.y *= -b->bounce;
        b->isGrounded = true;
        if (fabs(b->velocity.y) < 0.1f) b->velocity.y = 0;
    } else {
        b->isGrounded = false;
    }
    
    // Arena walls
    if (fabs(b->position.x) > ARENA_SIZE) {
        b->position.x = (b->position.x > 0) ? ARENA_SIZE : -ARENA_SIZE;
        b->velocity.x *= -b->bounce;
    }
    if (fabs(b->position.z) > ARENA_SIZE) {
        b->position.z = (b->position.z > 0) ? ARENA_SIZE : -ARENA_SIZE;
        b->velocity.z *= -b->bounce;
    }
    
    // Stop if nearly still
    if (Vector3Length(b->velocity) < 0.01f) {
        b->velocity = (Vector3){0, 0, 0};
    }
}

void ShootBall(Ball* b, Vector3 direction, float power) {
    if (b->isScored) return;
    b->velocity.x = direction.x * power;
    b->velocity.y = direction.y * power + 2.0f;
    b->velocity.z = direction.z * power;
    b->isGrounded = false;
}

void ResetBall(Ball* b) {
    b->position = (Vector3){0, BALL_RADIUS, 0};
    b->velocity = (Vector3){0, 0, 0};
    b->isGrounded = true;
    b->isScored = false;
}

// ============ NET FUNCTIONS ============
Net CreateNet(Vector3 center) {
    Net net;
    net.springConstant = 60.0f;
    net.damping = 0.85f;
    net.ballCaught = false;
    
    for (int row = 0; row < NET_ROWS; row++) {
        for (int col = 0; col < NET_COLS; col++) {
            float x = (float)col / (NET_COLS - 1) * NET_WIDTH - NET_WIDTH/2;
            float y = (float)row / (NET_ROWS - 1) * NET_HEIGHT;
            
            net.points[row][col].restPosition = (Vector3){
                center.x + x,
                center.y + y,
                center.z
            };
            net.points[row][col].position = net.points[row][col].restPosition;
            net.points[row][col].velocity = (Vector3){0, 0, 0};
            
            bool isEdge = (row == 0 || row == NET_ROWS-1 || 
                          col == 0 || col == NET_COLS-1);
            net.points[row][col].isFixed = isEdge;
        }
    }
    return net;
}

void UpdateNet(Net* net, Ball* ball, float dt) {
    net->ballCaught = false;
    
    for (int row = 1; row < NET_ROWS-1; row++) {
        for (int col = 1; col < NET_COLS-1; col++) {
            NetPoint* p = &net->points[row][col];
            if (p->isFixed) continue;
            
            // Ball-net collision
            Vector3 diff = Vector3Subtract(ball->position, p->position);
            float dist = Vector3Length(diff);
            
            if (dist < ball->radius + 0.3f && !ball->isScored) {
                Vector3 pushDir = Vector3Normalize(diff);
                float pushForce = 3.0f * (ball->radius + 0.3f - dist);
                p->position = Vector3Add(p->position, 
                                         Vector3Scale(pushDir, pushForce));
                
                // Slow ball
                ball->velocity = Vector3Scale(ball->velocity, 0.2f);
                ball->position = Vector3Add(ball->position,
                    Vector3Scale(pushDir, 0.05f));
                
                net->ballCaught = true;
            }
            
            // Spring force
            Vector3 displacement = Vector3Subtract(p->position, p->restPosition);
            Vector3 springForce = Vector3Scale(displacement, -net->springConstant);
            
            // Damping
            Vector3 dampingForce = Vector3Scale(p->velocity, -net->damping);
            
            // Apply forces
            Vector3 acceleration = Vector3Add(springForce, dampingForce);
            p->velocity = Vector3Add(p->velocity, Vector3Scale(acceleration, dt));
            p->position = Vector3Add(p->position, Vector3Scale(p->velocity, dt));
            
            // Clamp to prevent explosion
            float maxDist = 1.0f;
            if (Vector3Length(displacement) > maxDist) {
                p->position = Vector3Add(p->restPosition, 
                                         Vector3Scale(Vector3Normalize(displacement), maxDist));
            }
        }
    }
}

bool CheckGoal(Ball* ball, Net* net) {
    if (ball->isScored) return false;
    
    Vector3 netCenter = net->points[NET_ROWS/2][NET_COLS/2].restPosition;
    
    // Ball must cross net plane
    if (ball->position.z < netCenter.z - 0.5f) {
        // Inside goal posts
        if (fabs(ball->position.x) < NET_WIDTH/2 - 0.3f) {
            if (ball->position.y < NET_HEIGHT - 0.3f) {
                return true;
            }
        }
    }
    return false;
}

// ============ GAME FUNCTIONS ============
void DrawPitch() {
    // Ground
    DrawPlane((Vector3){0, 0, 0}, (Vector2){40, 30}, (Color){34, 139, 34, 255});
    
    // Center circle
    DrawCircle3D((Vector3){0, 0.05f, 0}, 3.0f, (Vector3){1, 0, 0}, 90, WHITE);
    
    // Center line
    DrawLine3D((Vector3){0, 0.05f, -15}, (Vector3){0, 0.05f, 15}, WHITE);
    
    // Goal area
    DrawLine3D((Vector3){-4, 0.05f, -15}, (Vector3){4, 0.05f, -15}, WHITE);
    DrawLine3D((Vector3){-4, 0.05f, -12}, (Vector3){-4, 0.05f, -15}, WHITE);
    DrawLine3D((Vector3){4, 0.05f, -12}, (Vector3){4, 0.05f, -15}, WHITE);
}

void DrawNet(Net* net) {
    for (int row = 0; row < NET_ROWS; row++) {
        for (int col = 0; col < NET_COLS; col++) {
            NetPoint p = net->points[row][col];
            
            Color netColor = (row == 0 || row == NET_ROWS-1 || 
                             col == 0 || col == NET_COLS-1) ? RED : WHITE;
            
            // Horizontal connections
            if (col < NET_COLS - 1) {
                DrawLine3D(p.position, net->points[row][col+1].position, netColor);
            }
            // Vertical connections
            if (row < NET_ROWS - 1) {
                DrawLine3D(p.position, net->points[row+1][col].position, netColor);
            }
        }
    }
}

void DrawGoalPosts() {
    // Posts
    DrawCube((Vector3){-NET_WIDTH/2, NET_HEIGHT/2, GOAL_Z}, 
             0.15f, NET_HEIGHT, 0.15f, RED);
    DrawCube((Vector3){ NET_WIDTH/2, NET_HEIGHT/2, GOAL_Z}, 
             0.15f, NET_HEIGHT, 0.15f, RED);
    
    // Crossbar
    DrawCube((Vector3){0, NET_HEIGHT, GOAL_Z}, 
             NET_WIDTH, 0.15f, 0.15f, RED);
    
    // Back of net (simple rectangle)
    DrawRectangleV((Vector2){-NET_WIDTH/2, 0}, (Vector2){NET_WIDTH, NET_HEIGHT}, 
                   (Color){200, 200, 200, 50});
}

void DrawUI(GameState* state, Shooter* shooter, Ball* ball) {
    // Score
    DrawText(TextFormat("SCORE: %d", state->score), 10, 10, 30, GOLD);
    DrawText(TextFormat("Attempts: %d", state->attempts), 10, 50, 20, WHITE);
    
    // Power bar
    if (shooter->isCharging) {
        DrawRectangle(10, 100, 30, 200, (Color){50, 50, 50, 200});
        float powerHeight = (shooter->power / MAX_POWER) * 200;
        DrawRectangle(10, 300 - powerHeight, 30, powerHeight, RED);
        DrawText(TextFormat("%.0f%%", shooter->power/MAX_POWER*100), 
                 10, 310, 15, WHITE);
    }
    
    // Instructions
    DrawText("Click on ball to shoot!", 10, 400, 20, WHITE);
    DrawText("Hold click for power", 10, 430, 15, (Color){200, 200, 200, 200});
    DrawText("Press R to reset ball", 10, 460, 15, (Color){200, 200, 200, 200});
    
    // Ball info
    float speed = Vector3Length(ball->velocity);
    DrawText(TextFormat("Ball speed: %.1f", speed), 10, 500, 15, 
             speed > 5 ? GREEN : WHITE);
    
    if (state->isGoal) {
        DrawText("GOAL!", SCREEN_WIDTH/2 - 100, SCREEN_HEIGHT/2 - 50, 80, GOLD);
        DrawText("+1 POINT!", SCREEN_WIDTH/2 - 80, SCREEN_HEIGHT/2 + 30, 40, WHITE);
    }
}

// ============ MAIN GAME ============
int main() {
    // Window
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Nationalball - Open Source Football");
    
    // Camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){12.0f, 8.0f, 8.0f};
    camera.target = (Vector3){0, 1.0f, -3.0f};
    camera.up = (Vector3){0, 1.0f, 0};
    camera.fovy = 50.0f;
    
    // Game objects
    Ball ball = CreateBall((Vector3){0, BALL_RADIUS, 0});
    Net net = CreateNet((Vector3){0, 1.5f, GOAL_Z});
    GameState state = {0, 0, 0, false, 0};
    Shooter shooter = {{0}, 0, false, false};
    
    SetTargetFPS(60);
    
    // Main loop
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        
        // ============ INPUT ============
        // Camera controls
        if (IsKeyDown(KEY_LEFT)) camera.position.x -= 5.0f * dt;
        if (IsKeyDown(KEY_RIGHT)) camera.position.x += 5.0f * dt;
        if (IsKeyDown(KEY_UP)) camera.position.z += 5.0f * dt;
        if (IsKeyDown(KEY_DOWN)) camera.position.z -= 5.0f * dt;
        
        // Reset ball
        if (IsKeyPressed(KEY_R)) {
            ResetBall(&ball);
            state.isGoal = false;
        }
        
        // Shooting
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Ray ray = GetMouseRay(GetMousePosition(), camera);
            RayCollision collision = GetRayCollisionSphere(ray, ball.position, ball.radius);
            
            if (collision.hit && !ball.isScored) {
                shooter.isClicked = true;
                shooter.isCharging = true;
                shooter.power = 0;
            }
        }
        
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && shooter.isCharging) {
            shooter.power += dt * 15.0f;
            if (shooter.power > MAX_POWER) shooter.power = MAX_POWER;
        }
        
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && shooter.isCharging) {
            if (shooter.power > 1.0f) {
                // Shoot toward goal
                Vector3 goalCenter = (Vector3){0, 1.5f, GOAL_Z - 1.0f};
                Vector3 direction = Vector3Normalize(
                    Vector3Subtract(goalCenter, ball.position)
                );
                // Add slight random deviation for realism
                direction.x += (rand() % 100 - 50) / 1000.0f;
                direction.y += (rand() % 100 - 50) / 1000.0f;
                direction = Vector3Normalize(direction);
                
                ShootBall(&ball, direction, shooter.power);
                state.attempts++;
                shooter.isCharging = false;
                shooter.isClicked = false;
                shooter.power = 0;
            } else {
                shooter.isCharging = false;
                shooter.isClicked = false;
                shooter.power = 0;
            }
        }
        
        // ============ UPDATE ============
        UpdateBall(&ball, dt);
        UpdateNet(&net, &ball, dt);
        
        // Goal check
        if (CheckGoal(&ball, &net) && !state.isGoal) {
            state.score++;
            state.isGoal = true;
            state.goalTimer = 2.0f;
            ball.isScored = true;
        }
        
        if (state.isGoal) {
            state.goalTimer -= dt;
            if (state.goalTimer <= 0) {
                state.isGoal = false;
                ResetBall(&ball);
            }
        }
        
        // ============ DRAW ============
        BeginDrawing();
        ClearBackground((Color){20, 30, 40, 255});
        
        BeginMode3D(camera);
        
        DrawPitch();
        DrawNet(&net);
        DrawGoalPosts();
        
        // Draw ball with glow
        DrawSphere(ball.position, ball.radius, 
                   ball.isScored ? GOLD : ORANGE);
        DrawSphereWires(ball.position, ball.radius + 0.1f, 16, 16, 
                        ball.isScored ? GOLD : (Color){255, 165, 0, 100});
        
        EndMode3D();
        
        // UI
        DrawUI(&state, &shooter, &ball);
        
        // Open source badge
        DrawText("GPL v3 - Open Source", SCREEN_WIDTH - 250, SCREEN_HEIGHT - 30, 15, 
                 (Color){100, 100, 100, 150});
        DrawText("github.com/yourname/nationalball", SCREEN_WIDTH - 250, SCREEN_HEIGHT - 10, 15, 
                 (Color){100, 100, 100, 150});
        
        EndDrawing();
    }
    
    CloseWindow();
    return 0;
}
