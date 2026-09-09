#pragma once
namespace ShaderSource {
const char* const GeometryVertex=R"GLSL(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec4 tint;
layout(location=2) in vec3 surface;
uniform vec2 viewport;
uniform vec2 worldOffset;
out vec4 color;
out vec2 uv;
out vec2 world;
flat out int material;
void main(){
 gl_Position=vec4(position.x/viewport.x*2.-1.,1.-position.y/viewport.y*2.,0,1);
 color=tint; uv=surface.xy; material=int(surface.z+.5); world=position+worldOffset;
}
)GLSL";
const char* const GeometryFragment=R"GLSL(#version 330 core
in vec4 color; in vec2 uv; in vec2 world; flat in int material;
uniform sampler2D glyph;
out vec4 result;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);}
void main(){
 if(material==6){result=vec4(color.rgb,color.a*texture(glyph,uv).a);return;}
 vec3 c=color.rgb;
 float grain=noise(world*.23), damp=noise(world*.019);
 if(material==1){
  vec2 p=world/vec2(27,13);p.x+=mod(floor(p.y),2.)*.5;
  vec2 f=fract(p);float seam=1.-smoothstep(.025,.10,min(min(f.x,1.-f.x),min(f.y,1.-f.y)));
  c*=.85+.22*grain-.28*seam;c=mix(c,c*vec3(.65,.85,.77),smoothstep(.57,.83,damp)*.36);
 }else if(material==2){c*=.76+.36*grain;c=mix(c,c*vec3(.76,.90,.75),damp*.4);
 }else if(material==3){float lines=sin(world.y*.72+noise(world*.025)*8.);c*=.78+.17*grain+.12*lines;
 }else if(material==4){c*=.91+.06*sin(world.y*1.2)*sin(world.x*1.2);
 }else if(material==5){
  vec2 p=vec2(world.x*.5+world.y,world.y-world.x*.5)/19.;
  p.x+=mod(floor(p.y),2.)*.5;vec2 f=fract(p);
  float seam=1.-smoothstep(.018,.075,min(min(f.x,1.-f.x),min(f.y,1.-f.y)));
  c*=.87+.19*grain-.32*seam;c+=vec3(.025,.036,.039)*smoothstep(.65,.86,damp);
 }
 result=vec4(c,color.a);
}
)GLSL";
const char* const ScreenVertex=R"GLSL(#version 330 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}
)GLSL";
const char* const BlurFragment=R"GLSL(#version 330 core
in vec2 uv;out vec4 result;
uniform sampler2D sourceImage;uniform vec2 direction;uniform int extractLight;
vec3 sampleLight(vec2 p){vec3 c=texture(sourceImage,p).rgb;return extractLight==1?max(c-vec3(.56),0.):c;}
void main(){vec3 c=sampleLight(uv)*.227027;
 c+=(sampleLight(uv+direction*1.384615)+sampleLight(uv-direction*1.384615))*.316216;
 c+=(sampleLight(uv+direction*3.230769)+sampleLight(uv-direction*3.230769))*.070270;
 result=vec4(c,1);}
)GLSL";
const char* const PostFragment=R"GLSL(#version 330 core
in vec2 uv;out vec4 result;
uniform sampler2D scene;uniform sampler2D bloom;uniform vec2 texel;uniform float time;
float luma(vec3 c){return dot(c,vec3(.299,.587,.114));}
vec3 antialias(){
 vec3 m=texture(scene,uv).rgb;
 vec3 nw=texture(scene,uv+vec2(-1,1)*texel).rgb,ne=texture(scene,uv+vec2(1,1)*texel).rgb;
 vec3 sw=texture(scene,uv+vec2(-1,-1)*texel).rgb,se=texture(scene,uv+vec2(1,-1)*texel).rgb;
 float a=luma(nw),b=luma(ne),c=luma(sw),d=luma(se),e=luma(m);
 float low=min(e,min(min(a,b),min(c,d))),high=max(e,max(max(a,b),max(c,d)));
 if(high-low<max(.025,high*.12))return m;
 vec2 dir=vec2(-((a+b)-(c+d)),(a+c)-(b+d));
 float reduce=max((a+b+c+d)*.03125,.0078125);
 dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),vec2(-6),vec2(6))*texel;
 vec3 aa=.5*(texture(scene,uv+dir*(-1./6.)).rgb+texture(scene,uv+dir*(1./6.)).rgb);
 vec3 bb=aa*.5+.25*(texture(scene,uv-dir*.5).rgb+texture(scene,uv+dir*.5).rgb);
 float lb=luma(bb);return lb<low||lb>high?aa:bb;
}
void main(){
 vec3 c=antialias()+texture(bloom,uv).rgb*.40;
 float light=luma(c);c=mix(vec3(light),c,.86);
 c*=mix(vec3(.86,.99,1.055),vec3(1.06,1.015,.94),smoothstep(.08,.65,light));
 c=1.-exp(-c*1.18);
 float vignette=smoothstep(.27,.78,length((uv-.5)*vec2(1.05,.88)));
 c*=1.-vignette*.31;
 float grain=fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233))+floor(time*12.))*43758.5453)-.5;
 c+=grain*.0035;
 result=vec4(clamp(c,0.,1.),1);
}
)GLSL";
}
