#pragma once
#include "Dependencies/glew.h"
#include <string>
#include <vector>
#include <unordered_map>
struct Point { float x, y; };
struct Color {
    float r, g, b, a;
    Color(float R, float G, float B, float A=1):r(R),g(G),b(B),a(A){}
};
enum class Material { Plain=0, Stone=1, Earth=2, Wood=3, Cloth=4, Paving=5 };
class Renderer {
public:
    Renderer(int width, int height);
    ~Renderer();
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    bool IsInitialized() const { return program && postProgram && blurProgram && ready; }
    void Resize(int width,int height);
    void Begin(float time=0, Point worldOffset={0,0});
    void FinishScene();
    void Flush();
    void Triangle(Point a,Point b,Point c,Color color);
    void Quad(Point a,Point b,Point c,Point d,Color color);
    void Rect(float x,float y,float w,float h,Color color);
    void Ellipse(float x,float y,float rx,float ry,Color color);
    void SoftEllipse(float x,float y,float rx,float ry,Color color);
    void Line(Point a,Point b,float width,Color color);
    void Panel(float x,float y,float w,float h);
    void SetMaterial(Material value) { material=value; }
    void Text(float x,float baseline,const std::string& utf8,Color color,int size=19);
    float TextWidth(const std::string& utf8,int size=19);
    void WrappedText(float x,float baseline,float maxWidth,const std::string& utf8,Color color,int size=19);
    int Width() const { return width; }
    int Height() const { return height; }
private:
    struct Vertex { float x,y,r,g,b,a,u,v,kind; };
    struct Label { GLuint texture=0; int width=0,height=0; };
    std::vector<Vertex> vertices;
    std::unordered_map<std::string,Label> labels;
    GLuint program=0,postProgram=0,blurProgram=0,buffer=0,vao=0;
    GLuint sceneFbo=0,sceneColor=0,resolveFbo=0,sceneTexture=0;
    GLuint bloomFbo[2]={},bloomTexture[2]={};
    int width=1,height=1,bloomWidth=1,bloomHeight=1,samples=1;
    bool ready=false;
    float clock=0; Point offset={0,0}; Material material=Material::Plain;
    void* fontDC=nullptr;
    GLuint Compile(const char* vertex,const char* fragment);
    void ReleaseTargets();
    void Fullscreen(GLuint shader);
    Label& GetLabel(const std::string& utf8,int size);
    void Push(Point p,Color c,float u=0,float v=0,float kind=-1);
};
