#include "stdafx.h"
#include "Game.h"
#include <algorithm>
#include <cmath>
namespace {
const float pi=3.14159265f;
Color Scale(Color c,float amount){return Color(c.r*amount,c.g*amount,c.b*amount,c.a);}
}
void Game::Ground(Renderer& r,float x,float y,float w,float h,Color c,float z){
    r.Quad(Project(x-w/2,y-h/2,z,r),Project(x+w/2,y-h/2,z,r),Project(x+w/2,y+h/2,z,r),Project(x-w/2,y+h/2,z,r),c);
}
void Game::Sigil(Renderer& r,Point p,float size,Color c){
    r.Line({p.x,p.y-size},{p.x,p.y+size},1.5f,c);
    r.Line({p.x-size*.65f,p.y-size*.25f},{p.x+size*.65f,p.y-size*.25f},1.5f,c);
    r.Line({p.x-size*.42f,p.y+size*.25f},{p.x+size*.42f,p.y+size*.25f},1,c);
    r.Quad({p.x,p.y-size*1.3f},{p.x+size*.22f,p.y-size*.8f},{p.x,p.y-size*.6f},{p.x-size*.22f,p.y-size*.8f},c);
}
void Game::Box(Renderer& r,const Object& o,float height,Color c,Material mat){
    float warmth=0;
    for(const auto& light:objects)if(light.type=="lamp"){
        float attenuation=std::max(0.f,1-Dist(o.x,o.y,light.x,light.y)/6);
        warmth+=attenuation*attenuation*(.9f+.1f*sinf(time*3.1f+light.x));
    }
    warmth=std::min(warmth,1.f);c.r+=warmth*.18f;c.g+=warmth*.045f;
    // Cut away nearby foreground architecture rather than hiding the player.
    Point feet=Project(px,py,0,r),center=Project(o.x,o.y,0,r);
    if(o.x+o.y>px+py&&std::abs(center.x-feet.x)<(o.w+o.h)*17+14&&feet.y>center.y-height-50&&feet.y<center.y+15)c.a*=.35f;
    Point a=Project(o.x-o.w/2,o.y-o.h/2,height,r),b=Project(o.x+o.w/2,o.y-o.h/2,height,r);
    Point d=Project(o.x-o.w/2,o.y+o.h/2,height,r),e=Project(o.x+o.w/2,o.y+o.h/2,height,r);
    Point bb={b.x,b.y+height},dd={d.x,d.y+height},ee={e.x,e.y+height};
    r.SetMaterial(mat);r.Quad(b,bb,ee,e,Scale(c,.54f));r.Quad(d,e,ee,dd,Scale(c,.77f));r.Quad(a,b,e,d,c);
    r.SetMaterial(Material::Plain);
    r.Line(a,b,.8f,Color(.56f,.59f,.52f,.20f*c.a));r.Line(d,e,1,Color(.55f,.57f,.5f,.27f*c.a));
}
void Game::DrawGround(Renderer& r){
    for(int y=0;y<(int)mapHeight;++y)for(int x=0;x<(int)mapWidth;++x){
        Point p=Project(x+.5f,y+.5f,0,r);if(p.x<-70||p.x>r.Width()+70||p.y<-40||p.y>r.Height()+40)continue;
        bool road=false;for(const auto& o:objects)if(o.type=="road"&&SegmentDistance(x+.5f,y+.5f,o.x,o.y,o.w,o.h)<o.height){road=true;break;}
        float n=((x*17+y*31)%11)*.003f;
        bool cemetery=y>27&&x<32;
        r.SetMaterial(road?Material::Paving:Material::Earth);
        Ground(r,x+.5f,y+.5f,1.015f,1.015f,road?Color(.235f+n,.24f+n,.23f+n):cemetery?Color(.14f+n,.15f+n,.135f+n):Color(.125f+n,.155f+n,.15f+n));
        r.SetMaterial(Material::Plain);
        if(!road&&(x*23+y*7)%9==0){
            for(int j=0;j<3;++j){float h=4+(x+j*y)%8;r.Line({p.x+j*4,p.y},{p.x+j*4-3,p.y-h},1,Color(.31f,.34f,.27f,.45f));}
        }
        if(road&&(x*7+y*3)%6==0){
            r.Ellipse(p.x,p.y,20,7,Color(.045f,.065f,.07f,.40f));
            float glint=.07f+.025f*sinf(time*.6f+x);r.Line({p.x-11,p.y+1},{p.x+8,p.y-2},.7f,Color(.5f,.61f,.6f,glint));
        }
    }
    // Blood channels and pale growth break up the repeated terrain at world-fixed positions.
    for(int i=0;i<100;++i){
        float x=2+(i*37%480)/10.f,y=2+(i*53%350)/10.f;Point p=Project(x,y,0,r);
        if(p.x<-80||p.x>r.Width()+80||p.y<-60||p.y>r.Height()+60)continue;
        r.SoftEllipse(p.x,p.y,29+i%19,10+i%7,Color(.2f,.025f,.048f,.6f));
        r.Line({p.x-14,p.y+1},{p.x+12,p.y-2},1.2f,Color(.29f,.08f,.085f,.45f));
        if(i%3==0)for(int j=0;j<4;++j){
            float ox=j*8.f;r.Line({p.x,p.y},{p.x+ox+12,p.y-6-j*3},1,Color(.44f,.45f,.34f,.45f));
            r.Line({p.x+ox+6,p.y-5-j*2},{p.x+ox+3,p.y-13-j*3},.8f,Color(.44f,.45f,.34f,.4f));
        }
    }
    for(const auto& f:footsteps){Point p=Project(f.x,f.y,0,r);float a=1-f.age/3;
        r.Ellipse(p.x,p.y+2,5,2,Color(.035f,.045f,.045f,a*.45f));
        if(f.age<.55f){float radius=3+f.age*17;for(int i=0;i<20;++i){float a0=i*pi/10,a1=(i+1)*pi/10;r.Line({p.x+cosf(a0)*radius,p.y+sinf(a0)*radius*.4f},{p.x+cosf(a1)*radius,p.y+sinf(a1)*radius*.4f},.6f,Color(.34f,.4f,.38f,(.55f-f.age)*.15f));}}
    }
    for(const auto& o:objects)if(o.type=="lamp"){
        Point p=Project(o.x,o.y,0,r);float flicker=.93f+.07f*sinf(time*4.1f+o.y);
        r.SoftEllipse(p.x,p.y,170,83,Color(.78f,.24f,.12f,.30f*flicker));
        r.SoftEllipse(p.x,p.y+8,52,15,Color(.85f,.27f,.13f,.19f*flicker));
    }
}
void Game::DrawShadows(Renderer& r){
    r.SetMaterial(Material::Plain);
    for(const auto& o:objects){
        if(o.type=="road"||o.type=="root"||o.type=="lore")continue;
        Point p=Project(o.x,o.y,0,r);float height=o.height>0?o.height:18;
        if(p.x<-250||p.x>r.Width()+250||p.y<-200||p.y>r.Height()+200)continue;
        float dx=.6f,dy=.32f,nearest=7;
        for(const auto& light:objects)if(light.type=="lamp"){
            float d=Dist(o.x,o.y,light.x,light.y);if(d>.15f&&d<nearest){nearest=d;Point l=Project(light.x,light.y,0,r);dx=p.x-l.x;dy=p.y-l.y;float len=std::sqrt(dx*dx+dy*dy);dx=dx/len*.8f;dy=dy/len*.5f;}
        }
        float length=std::min(90.f,height*.7f),width=std::max(8.f,(o.w+o.h)*10);
        for(int pass=2;pass>=0;--pass){
            float spread=pass*3.f;Color shade(.006f,.012f,.016f,pass==0?.20f:.055f);
            r.Quad({p.x-width-spread,p.y},{p.x+width+spread,p.y},
                {p.x+width*.7f+dx*length+spread,p.y+dy*length+8+spread},
                {p.x-width*.7f+dx*length-spread,p.y+dy*length+8+spread},shade);
        }
        r.SoftEllipse(p.x,p.y+3,width*1.65f,12+o.h*5,Color(.005f,.009f,.012f,.58f));
    }
}
void Game::Person(Renderer& r,float x,float y,bool player){
    Point p=Project(x,y,0,r);r.SoftEllipse(p.x+6,p.y+4,31,12,Color(0,0,0,.66f));
    float breath=sinf(time*(player?1.9f:2.8f))*(player?.6f:1.1f);
    if(player){
        float step=sinf(walkPhase)*5*motion,bob=std::abs(sinf(walkPhase))*1.8f*motion;
        float sway=sinf(time*2.2f+walkPhase*.35f)*(1+motion*2);
        float torso=p.y-31-bob+breath,head=torso-20;
        Color leather(.20f,.16f,.13f),cloth(.20f,.265f,.27f),metal(.45f,.47f,.43f);
        // Articulated boots, shins, shoulders, hood, bandages, belt and blood vessel.
        r.Line({p.x-5,p.y-19},{p.x-6+step*.35f,p.y-4+step*.45f},6,Color(.14f,.14f,.13f));
        r.Line({p.x+5,p.y-19},{p.x+6-step*.35f,p.y-4-step*.45f},6,Color(.23f,.21f,.18f));
        r.Ellipse(p.x-6+step*.35f+facing*2,p.y-2+step*.45f,6,3,leather);
        r.Ellipse(p.x+6-step*.35f+facing*2,p.y-2-step*.45f,6,3,leather);
        r.SetMaterial(Material::Cloth);
        r.Quad({p.x-9,torso-8},{p.x+10,torso-7},{p.x+12+sway,p.y-12},{p.x-13+sway,p.y-10},cloth);
        r.Triangle({p.x-8,torso-7},{p.x-17+sway,p.y-9},{p.x-4,p.y-13},Scale(cloth,.6f));
        r.Triangle({p.x+3,torso-6},{p.x+14+sway,p.y-12},{p.x+5,p.y-18},Scale(cloth,1.2f));
        r.SetMaterial(Material::Plain);
        r.Line({p.x-7,torso-4},{p.x+5,p.y-14},3,leather);
        r.Line({p.x-9,p.y-20-bob},{p.x+11,p.y-21-bob},4,leather);
        r.Rect(p.x-1,p.y-23-bob,4,4,Color(.65f,.49f,.28f));
        float arm=step*.65f;
        r.Line({p.x-10,torso},{p.x-15,torso+13+arm},5,Color(.27f,.28f,.24f));
        r.Line({p.x+10,torso},{p.x+15,torso+13-arm},5,Color(.35f,.36f,.31f));
        for(int i=0;i<3;++i)r.Line({p.x+12,torso+7+i*3-arm},{p.x+17,torso+8+i*3-arm},1,Color(.6f,.57f,.44f));
        r.Ellipse(p.x-15,torso+15+arm,3,4,Color(.59f,.54f,.43f));
        r.Line({p.x+16,torso+14-arm},{p.x+20,p.y+1-arm},2,metal);
        r.Line({p.x+12,torso+16-arm},{p.x+19,torso+15-arm},2,Color(.6f,.48f,.28f));
        r.Ellipse(p.x,head+2,10,12,Color(.105f,.14f,.15f));
        r.Triangle({p.x-10,head},{p.x,head-14},{p.x+9,head+2},cloth);
        r.Ellipse(p.x+facing*2,head+5,5,6,Color(.55f,.52f,.43f));
        r.Rect(p.x-4+facing*2,head+3,8,3,Color(.065f,.08f,.08f));
        r.Line({p.x-8,head+12},{p.x+8,head+12},3,Color(.31f,.30f,.25f));
        r.Line({p.x-10,torso-8},{p.x-7,head+10},1.5f,metal);
        float vialY=torso+16+arm;
        r.Rect(p.x-18,vialY,6,10,Color(.45f,.065f,.09f));r.Rect(p.x-17,vialY-2,4,3,Color(.6f,.49f,.32f));
        r.Line({p.x-17,vialY+2},{p.x-17,vialY+7},1,Color(.95f,.4f,.35f));
        r.SoftEllipse(p.x-15,vialY+5,18,20,Color(.8f,.055f,.1f,.15f));
    }else{
        if(answer)breath=0;
        float cough=answer?0.f:std::pow(std::max(0.f,sinf(time*.7f)),12)*2;
        float chest=p.y-19+breath+cough;
        r.Line({p.x-5,p.y-8},{p.x-21,p.y+1},8,Color(.23f,.19f,.17f));
        r.Line({p.x+4,p.y-7},{p.x+21,p.y-1},7,Color(.19f,.18f,.15f));
        r.Ellipse(p.x-23,p.y+2,7,3,Color(.13f,.12f,.11f));
        r.SetMaterial(Material::Cloth);r.Quad({p.x-10,chest-7},{p.x+8,chest-4},{p.x+13,p.y-4},{p.x-12,p.y-3},Color(.31f,.28f,.24f));r.SetMaterial(Material::Plain);
        r.Line({p.x-8,chest-2},{p.x-18,chest+10},4,Color(.58f,.52f,.4f));
        r.Line({p.x+8,chest},{p.x+20,chest+13},4,Color(.44f,.45f,.33f));
        r.Ellipse(p.x-4,chest-14,7,9,Color(.51f,.52f,.4f));
        r.Line({p.x-9,chest-14},{p.x+1,chest-12},2,Color(.10f,.12f,.11f));
        for(int i=0;i<6;++i){float h=15+i%3*7+(given?reaction*7:0);float bend=sinf(time*1.3f+i)*1.5f;
            r.Line({p.x+4+i*3,p.y-8},{p.x+8+i*4+bend,p.y-h},2,Color(.52f,.53f,.37f));
            r.Line({p.x+8+i*4+bend,p.y-h},{p.x+3+i*4,p.y-h-9},1,Color(.68f,.63f,.43f));
            if(given)r.Ellipse(p.x+8+i*4+bend,p.y-h,2,2,Color(.7f,.10f,.13f,.6f));
        }
        Sigil(r,{p.x-1,chest+4},5,Color(.57f,.41f,.27f));
    }
}
void Game::DrawObject(Renderer& r,const Object& o){
    Point p=Project(o.x,o.y,0,r);int v=o.variant;
    if(o.type=="npc"){Person(r,o.x,o.y,false);return;}
    if(o.type=="wall"){
        float height=o.height>0?o.height:58;
        // Individual, uneven masonry bays form a broken silhouette instead of one cuboid.
        int bays=std::max(2,(int)(std::max(o.w,o.h)*1.6f));
        for(int j=0;j<bays;++j){Object part=o;bool alongX=o.w>=o.h;
            if(alongX){part.w=o.w/bays;part.x=o.x-o.w*.5f+part.w*(j+.5f);}else{part.h=o.h/bays;part.y=o.y-o.h*.5f+part.h*(j+.5f);}
            float broken=height*(.62f+.38f*((j*7+v*3)%9)/8.f);
            Box(r,part,broken,Color(.38f,.39f,.35f));
        }
        if(v%3==0){Point center=Project(o.x,o.y,height*.5f,r);Sigil(r,center,12,Color(.38f,.17f,.14f,.7f));}
    }else if(o.type=="pillar"){
        Box(r,o,10,Color(.40f,.41f,.36f));Object shaft=o;shaft.w*=.62f;shaft.h*=.62f;Box(r,shaft,o.height,Color(.36f,.38f,.34f));
        if(v%2==0)Sigil(r,{p.x,p.y-o.height*.65f},10,Color(.65f,.55f,.35f));
    }else if(o.type=="arch"||o.type=="exit"){
        float h=o.type=="exit"?105:o.height;
        for(int side=-1;side<=1;side+=2){Object column={"pillar",o.x+side*1.2f,o.y,.55f,.75f,h,0};Box(r,column,h,Color(.36f,.38f,.34f));}
        Point top=Project(o.x,o.y,h+24,r),left=Project(o.x-1.2f,o.y,h,r),right=Project(o.x+1.2f,o.y,h,r);
        r.Line(left,top,12,Color(.31f,.33f,.29f));r.Line(top,right,10,Color(.38f,.4f,.34f));
        if(o.type=="exit")Sigil(r,{top.x,top.y+13},8,Color(.66f,.53f,.34f));
        else{r.Line({top.x+5,top.y+12},{top.x+8,top.y+33},2,Color(.3f,.26f,.18f));}
    }else if(o.type=="rubble"){
        if(v%3==0){for(int i=0;i<5;++i){Object stone=o;stone.x+=((i*7)%5-2)*.22f;stone.y+=(i%3-1)*.25f;stone.w=.35f+(i%3)*.16f;stone.h=.35f+(i%2)*.2f;Box(r,stone,5+(i*3+v)%13,Color(.31f,.33f,.30f));}}
        else if(v%3==1){r.SetMaterial(Material::Wood);for(int i=0;i<4;++i)r.Line({p.x-24+i*5,p.y-9+i*3},{p.x+20-i*4,p.y+8-i*5},5,Color(.22f+i*.02f,.16f+i*.01f,.12f));r.SetMaterial(Material::Plain);}
        else{Box(r,o,8,Color(.26f,.28f,.25f));r.Ellipse(p.x+4,p.y-9,6,5,Color(.55f,.53f,.41f));r.Line({p.x-14,p.y-3},{p.x+11,p.y+4},3,Color(.51f,.5f,.39f));}
    }else if(o.type=="well"){
        for(int i=0;i<12;++i){float a=i*pi/6;Object stone={"",o.x+cosf(a)*.8f,o.y+sinf(a)*.8f,.5f,.5f,0,0};Box(r,stone,23+(i%3)*2,Color(.38f,.39f,.32f));}
        r.Ellipse(p.x,p.y-21,30,13,Color(.065f,.014f,.026f));
        for(int i=0;i<4;++i){float a=time*.4f+i;r.Line({p.x-20+i*11,p.y-20},{p.x-15+i*9+sinf(a)*2,p.y-39-i%2*15},2,Color(.46f,.42f,.31f));}
        r.SoftEllipse(p.x,p.y-19,40,19,Color(.62f,.03f,.08f,.17f));
    }else if(o.type=="altar"){
        Box(r,o,24,Color(.40f,.38f,.32f));Object plinth=o;plinth.w*=.6f;plinth.h*=.7f;Box(r,plinth,37,Color(.36f,.34f,.28f));
        r.Ellipse(p.x,p.y-38,16,6,Color(.22f,.018f,.044f));Sigil(r,{p.x,p.y-17},9,Color(.65f,.49f,.26f));
        for(int i=0;i<3;++i){float x=p.x-22+i*22;r.Rect(x,p.y-38,3,12,Color(.7f,.63f,.44f));r.Ellipse(x+1,p.y-40,2,3+sinf(time*4+i),Color(1.1f,.5f,.18f));}
    }else if(o.type=="wagon"){
        Box(r,o,15,Color(.30f,.22f,.15f),Material::Wood);
        for(int i=-1;i<=1;i+=2){float wx=p.x+i*27;r.Ellipse(wx,p.y-2,12,14,Color(.14f,.12f,.10f));r.Ellipse(wx,p.y-2,8,10,Color(.34f,.25f,.16f));for(int j=0;j<4;++j){float a=j*pi/4;r.Line({wx-cosf(a)*8,p.y-2-sinf(a)*10},{wx+cosf(a)*8,p.y-2+sinf(a)*10},2,Color(.14f,.12f,.1f));}}
        r.SetMaterial(Material::Cloth);r.Quad({p.x-23,p.y-26},{p.x+12,p.y-30},{p.x+27,p.y-11},{p.x-22,p.y-9},Color(.41f,.38f,.3f));r.SetMaterial(Material::Plain);
        r.Line({p.x-5,p.y-28},{p.x+10,p.y-12},3,Color(.30f,.075f,.07f));
        r.Line({p.x+25,p.y-8},{p.x+57,p.y+8},4,Color(.25f,.17f,.10f));
    }else if(o.type=="banner"){
        r.Line({p.x,p.y},{p.x,p.y-o.height},3,Color(.29f,.25f,.18f));
        float top=p.y-o.height+4,sway=sinf(time*1.7f+o.x)*4;
        r.SetMaterial(Material::Cloth);
        r.Quad({p.x+2,top},{p.x+30,top+2},{p.x+28+sway,top+26},{p.x+3+sway*.3f,top+25},Color(.31f,.065f,.09f));
        r.Triangle({p.x+3+sway*.3f,top+25},{p.x+28+sway,top+26},{p.x+23+sway,top+42},Color(.25f,.04f,.065f));r.SetMaterial(Material::Plain);
        Sigil(r,{p.x+15+sway*.3f,top+16},8,Color(.55f,.42f,.26f));
    }else if(o.type=="root"){
        for(int i=0;i<7;++i){float sway=sinf(time*.8f+i+o.x)*2;float height=12+(i*11+v)%35;
            Point tip={p.x-23+i*8+sway,p.y-height};r.Line({p.x,p.y},tip,3,Color(.35f,.36f,.25f));
            r.Line(tip,{tip.x-6,tip.y-11},1,Color(.59f,.56f,.36f));
            if(i%2==0)r.Ellipse(tip.x,tip.y,3,5,Color(.44f,.07f,.10f));
        }
    }else if(o.type=="grave"){
        Box(r,o,8,Color(.28f,.29f,.25f));Object head=o;head.w=.4f;head.h=.4f;Box(r,head,22+(v%3)*5,Color(.39f,.39f,.32f));
        Sigil(r,{p.x,p.y-20},7,Color(.2f,.21f,.18f));
    }else if(o.type=="body"||o.type=="corpse"){
        float flip=v%2?-1.f:1.f;r.SoftEllipse(p.x,p.y+2,35,11,Color(.26f,.02f,.035f,.62f));
        r.SetMaterial(Material::Cloth);
        r.Quad({p.x-17*flip,p.y-5},{p.x+7*flip,p.y-7},{p.x+17*flip,p.y+3},{p.x-11*flip,p.y+5},v%3==0?Color(.36f,.32f,.26f):v%3==1?Color(.20f,.245f,.245f):Color(.32f,.15f,.16f));r.SetMaterial(Material::Plain);
        r.Ellipse(p.x-21*flip,p.y-4,6,5,Color(.51f,.47f,.36f));
        r.Line({p.x+8*flip,p.y},{p.x+29*flip,p.y+5},5,Color(.18f,.16f,.14f));
        r.Line({p.x+8*flip,p.y},{p.x+24*flip,p.y-8},4,Color(.22f,.2f,.16f));
        r.Line({p.x-7*flip,p.y-3},{p.x-3*flip,p.y-14},3,Color(.48f,.43f,.33f));
        if(v%3==2){r.Line({p.x,p.y-4},{p.x+2,p.y-21},2,Color(.53f,.53f,.37f));r.Line({p.x+2,p.y-16},{p.x+10,p.y-26},1,Color(.58f,.55f,.38f));}
    }else if(o.type=="chest"){
        Box(r,o,18,Color(.33f,.24f,.18f),Material::Wood);r.Line({p.x-22,p.y-15},{p.x+19,p.y-15},2,Color(.52f,.4f,.24f));
        r.Quad({p.x-20,p.y-20},{p.x+11,p.y-25},{p.x+14,p.y-35},{p.x-18,p.y-30},Color(.27f,.19f,.14f));
        Sigil(r,{p.x-3,p.y-27},5,Color(.69f,.51f,.29f));
        if(!blood){float bob=sinf(time*1.8f)*1.3f;r.Rect(p.x-3,p.y-26+bob,7,12,Color(.85f,.065f,.16f));r.Rect(p.x-2,p.y-29+bob,5,4,Color(.68f,.54f,.30f));r.Line({p.x-2,p.y-24+bob},{p.x-2,p.y-18+bob},1,Color(1.25f,.52f,.49f));r.SoftEllipse(p.x,p.y-22,30,35,Color(.82f,.025f,.095f,.27f));}
    }else if(o.type=="lamp"){
        r.Line({p.x,p.y},{p.x,p.y-49},3,Color(.24f,.23f,.18f));r.Line({p.x-7,p.y-47},{p.x+7,p.y-47},2,Color(.51f,.39f,.23f));
        r.Quad({p.x-6,p.y-45},{p.x+6,p.y-45},{p.x+4,p.y-29},{p.x-4,p.y-29},Color(.66f,.20f,.07f));
        float f=sinf(time*6+o.x);r.Ellipse(p.x+f,p.y-37,2.5f,6+f,Color(1.4f,.71f,.26f));
        r.Line({p.x-7,p.y-29},{p.x+7,p.y-29},2,Color(.34f,.26f,.18f));
    }
}
void Game::DrawAtmosphere(Renderer& r){
    r.SetMaterial(Material::Plain);
    for(const auto& o:objects)if(o.type=="lamp"){
        Point p=Project(o.x,o.y,37,r);r.SoftEllipse(p.x,p.y,72,87,Color(.9f,.17f,.05f,.18f));
    }
    const Object* npc=Find("npc");if(npc&&given){Point p=Project(npc->x,npc->y,18,r);r.SoftEllipse(p.x,p.y,65+reaction*55,55+reaction*30,Color(.65f,.04f,.11f,.13f+.055f*sinf(time*2)));}
    // Layered world-space drifting spores: camera movement produces real parallax.
    for(int i=0;i<120;++i){
        float x=std::fmod(i*3.71f+time*.065f,mapWidth),y=std::fmod(i*2.37f+time*.028f,mapHeight);
        float z=12+(i*13)%85+sinf(time*.8f+i)*6;Point p=Project(x,y,z,r);
        if(p.x<0||p.x>r.Width()||p.y<0||p.y>r.Height())continue;
        float pulse=given?.5f+.5f*sinf(time*2):0;r.Ellipse(p.x,p.y,1+i%2*.4f,1+i%2*.4f,Color(.66f+pulse*.2f,.65f,.46f,.22f+pulse*.16f));
    }
    for(int i=0;i<9;++i){float x=std::fmod(4+i*6.f+time*.045f,mapWidth),y=5+i*3.6f;Point p=Project(x,y,12,r);r.SoftEllipse(p.x,p.y,250,51,Color(.38f,.47f,.48f,.055f));}
    if(target>=0){const auto& o=objects[target];Point p=Project(o.x,o.y,0,r);r.SoftEllipse(p.x,p.y+3,35,13,Color(.63f,.53f,.29f,.3f));}
}

