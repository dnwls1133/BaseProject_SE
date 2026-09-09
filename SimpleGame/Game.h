#pragma once
#include "Renderer.h"
#include <string>
#include <vector>
#include <unordered_map>
class Game {
public:
    bool Load(const std::string& path);
    void Key(unsigned char key,bool down);
    void Update(float dt);
    void Render(Renderer& renderer);
    void ClearInput();
private:
    struct Object { std::string type; float x=0,y=0,w=1,h=1,height=0; int variant=0; };
    struct DialogueLine { std::string speaker,text; };
    std::vector<Object> objects;
    std::unordered_map<std::string,std::vector<DialogueLine>> conversations;
    std::vector<DialogueLine> dialogue;
    bool keys[256]={};
    float mapWidth=52,mapHeight=40,spawnX=8,spawnY=7;
    float px=8,py=7,cx=8,cy=7,time=0,elapsed=0,walkPhase=0,motion=0,facing=1;
    float reaction=0,noticeTime=0,stepTime=0;
    struct Footprint {float x,y,age;};std::vector<Footprint> footsteps;
    bool paused=false,blood=false,given=false,accepted=false,choice=false,done=false;
    int answer=0,line=0,target=-1;
    std::string notice;
    static float Dist(float x,float y,float a,float b);
    static float SegmentDistance(float x,float y,float ax,float ay,float bx,float by);
    bool Solid(const Object& object)const;
    bool Blocked(float x,float y)const;
    bool Reach(const Object& object)const;
    const Object* Find(const std::string& type)const;
    void Say(const std::string& id);
    void Reset();
    void RefreshTarget();
    Point Project(float x,float y,float z,Renderer& renderer)const;
    void Ground(Renderer& r,float x,float y,float w,float h,Color color,float z=0);
    void Box(Renderer& r,const Object& object,float height,Color color,Material material=Material::Stone);
    void Person(Renderer& r,float x,float y,bool player);
    void DrawObject(Renderer& r,const Object& object);
    void DrawShadows(Renderer& r);
    void DrawGround(Renderer& r);
    void DrawAtmosphere(Renderer& r);
    void DrawUI(Renderer& r);
    void Sigil(Renderer& r,Point p,float size,Color color);
};
