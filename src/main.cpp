// ============================================================================
//  THE ATLAS OF IMPOSSIBLE WORLDS  |  Prototype 0.2 "Grand Observatory"
//  OpenGL 3.3 Core  -  GLFW  -  GLAD 2  -  GLM        (single-file, no assets)
//
//  Rendering features
//   * HDR pipeline: 4x MSAA float framebuffer -> bloom -> ACES tone-mapping,
//     vignette, chromatic fringe, film grain, cinematic fade-in
//   * Per-pixel lighting: warm key, orchid fill, hemispheric ambient, a point
//     light inside the Atlas core, Fresnel rim, sky reflections, distance haze
//   * Procedural materials: veined marble, brushed brass, glass, portals
//   * Procedural sky with nebula + sunset glow, 2400 twinkling stars,
//     a soft cloud sea, floating islands with inverted towers
//   * Luminous particles: manta trail, drifting pollen, click bursts
//
//  Primitive types:  GL_TRIANGLES  (everything solid)
//                    GL_LINES      (Atlas ring graduations, rays)
//                    GL_POINTS     (starfield, particles)
//
//  CONTROLS
//   Left click   rotate Atlas rings 45 deg (eased)   Scroll   scale green world
//   Right drag   orbit camera                        F11      full-screen
//   R  reset     SPACE  auto-orbit                   B        toggle bloom
//   P  screenshot (TGA)                              ESC      exit
// ============================================================================
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using glm::mat3;
using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

constexpr float PI = 3.14159265358979323846f;
constexpr float INTRO_LEN = 5.0f;   // seconds of cinematic fly-in
constexpr float DECK_Y = -0.3f;     // world height of the observatory floor

// ----------------------------------------------------------------------------
//  Vertex / Mesh
// ----------------------------------------------------------------------------
struct Vertex {
    vec3 pos{0.0f};
    vec3 nrm{0.0f, 1.0f, 0.0f};
    vec3 col{1.0f};
    vec4 aux{0.0f};   // points: (size, phase, alpha)   billboards: (cx, cy, size, alpha)
};
using VList = std::vector<Vertex>;

struct Mesh {
    GLuint vao = 0, vbo = 0;
    GLsizei count = 0;
    GLenum mode = GL_TRIANGLES;
    size_t capacity = 0;

    void upload(const VList& v, GLenum primitive, size_t dynamicCapacity = 0) {
        mode = primitive;
        count = static_cast<GLsizei>(v.size());
        capacity = dynamicCapacity ? dynamicCapacity : v.size();
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        if (dynamicCapacity) {
            glBufferData(GL_ARRAY_BUFFER, capacity * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
            if (!v.empty()) glBufferSubData(GL_ARRAY_BUFFER, 0, v.size() * sizeof(Vertex), v.data());
        } else {
            glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(), GL_STATIC_DRAW);
        }
        const GLsizei stride = sizeof(Vertex);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 0));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 3));
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 6));
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 9));
        for (int i = 0; i < 4; ++i) glEnableVertexAttribArray(i);
        glBindVertexArray(0);
    }
    void update(const VList& v) {
        count = static_cast<GLsizei>(std::min(v.size(), capacity));
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        if (count) glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Vertex), v.data());
    }
    void draw() const {
        if (!count) return;
        glBindVertexArray(vao);
        glDrawArrays(mode, 0, count);
        glBindVertexArray(0);
    }
    void release() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vbo = vao = 0;
    }
};

// ----------------------------------------------------------------------------
//  Shaders
// ----------------------------------------------------------------------------
static const char* GLSL_COMMON = R"GLSL(
float hash(vec3 p){ p=fract(p*0.3183099+vec3(0.1,0.2,0.3)); p*=17.0; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }
float noise(vec3 x){
    vec3 i=floor(x), f=fract(x); f=f*f*(3.0-2.0*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x), mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x), mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float fbm(vec3 p){ float a=0.5,s=0.0; for(int i=0;i<5;i++){ s+=a*noise(p); p=p*2.03+vec3(17.1,3.7,9.2); a*=0.5; } return s; }
uniform vec3 uSkyH, uSkyL, uSkyM, uSkyT, uSunDir;
vec3 skyBase(vec3 d){
    d=normalize(d);
    float h=d.y;
    vec3 c=mix(uSkyH*0.30, uSkyL*0.35, smoothstep(-0.02,0.10,h));
    c=mix(c, uSkyM, smoothstep(0.05,0.40,h));
    c=mix(c, uSkyT, smoothstep(0.30,0.95,h));
    c=mix(c, uSkyM*0.8, smoothstep(0.0,-0.45,h));
    c+=uSkyH*0.22*exp(-abs(h)*16.0);
    float s=max(dot(d,normalize(uSunDir)),0.0);
    c+=mix(uSkyH,vec3(1.0),0.4)*(pow(s,8.0)*0.22+pow(s,200.0)*2.5);
    return c;
}
)GLSL";

static const char* SCENE_VS = R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
uniform mat4 uModel, uViewProj;
uniform float uTime, uFlap, uFlapAmp, uFlapLen, uFlapSpd;
out vec3 vWorld, vN, vCol, vLocal;
void main(){
    vec3 p=aPos;
    if(uFlap>0.5){ float ax=abs(p.x); p.y+=sin(uTime*uFlapSpd-ax*1.1)*uFlapAmp*smoothstep(0.0,uFlapLen,ax); }
    vLocal=aPos;
    vec4 w=uModel*vec4(p,1.0);
    vWorld=w.xyz;
    vN=mat3(uModel)*aNrm;
    vCol=pow(aCol,vec3(2.2));
    gl_Position=uViewProj*w;
}
)GLSL";

static const char* SCENE_FS = R"GLSL(
in vec3 vWorld, vN, vCol, vLocal;
uniform vec3 uCam, uCoreLight, uCoreCol, uTint;
uniform float uTime, uSpec, uShin, uEmit, uRefl, uCoreI;
uniform int uMode;     // 0 lit  1 marble  2 brass  3 emissive  4 glass/fresnel  6 portal
out vec4 FragColor;
vec3 fogIt(vec3 c, vec3 V, float dist){
    float f=1.0-exp(-pow(dist*0.0075,1.5));
    return mix(c, skyBase(-V)*0.8, f);
}
void main(){
    vec3 toCam=uCam-vWorld;
    float dist=length(toCam);
    vec3 V=toCam/dist;
    vec3 N=normalize(vN);
    vec3 base=vCol*uTint;

    if(uMode==3){                                   // emissive
        float pulse=1.0+0.10*sin(uTime*2.0+vLocal.x*3.0+vLocal.y*2.0);
        FragColor=vec4(fogIt(base*uEmit*pulse,V,dist),1.0); return;
    }
    if(uMode==4){                                   // glass / atmosphere (additive)
        float ndv=abs(dot(N,V));
        float fres=pow(1.0-ndv,3.0);
        vec3 L=normalize(vec3(-0.5,0.7,0.4));
        float sheen=pow(max(dot(reflect(-L,N),V),0.0),90.0);
        vec3 c=base*(0.05+fres*uEmit)+vec3(1.0,0.95,0.9)*sheen*0.6;
        FragColor=vec4(c,1.0); return;
    }
    if(uMode==6){                                   // portal veil (additive)
        vec2 p=vLocal.xy-vec2(0.0,1.5);
        float r=length(p), a=atan(p.y,p.x);
        float sw=0.5+0.5*sin(a*3.0+r*5.0-uTime*1.4);
        float n=fbm(vec3(p*1.7,uTime*0.25));
        float rings=0.5+0.5*sin(r*9.0-uTime*2.2);
        vec3 c=uTint*(0.10+0.55*sw*n+0.25*rings*n)+uTint*pow(max(1.0-r*0.35,0.0),3.0)*0.4;
        FragColor=vec4(c,1.0); return;
    }

    if(dot(N,V)<0.0) N=-N;                          // robust to winding
    vec3 albedo=base;
    float spec=uSpec, refl=uRefl;
    if(uMode==1){                                   // veined marble
        vec3 p=vWorld*0.33;
        float w=fbm(p*1.7+vec3(fbm(p*3.1))*1.3);
        float vein=smoothstep(0.035,0.0,abs(w-0.5));
        albedo*=0.78+0.22*fbm(p*9.0);
        albedo=mix(albedo, vec3(0.30,0.22,0.46), vein*0.65);
    } else if(uMode==2){                            // brushed brass
        albedo*=0.80+0.35*noise(vec3(vLocal.x*55.0,vLocal.y*3.0,vLocal.z*55.0));
    }

    vec3 Lk=normalize(vec3(-0.45,0.75,0.50));
    vec3 Lf=normalize(vec3(0.60,0.20,-0.70));
    vec3 keyC=vec3(1.0,0.80,0.62)*1.05;
    vec3 fillC=vec3(0.42,0.28,0.95)*0.55;
    vec3 hemi=mix(vec3(0.03,0.02,0.07),vec3(0.17,0.13,0.32),N.y*0.5+0.5);
    vec3 toL=uCoreLight-vWorld; float d2=dot(toL,toL);
    vec3 Lc=toL*inversesqrt(d2);
    vec3 coreC=uCoreCol*uCoreI/(1.0+0.28*d2);

    float nk=max(dot(N,Lk),0.0), nf=max(dot(N,Lf),0.0), nc=max(dot(N,Lc),0.0);
    vec3 diff=albedo*(hemi+keyC*nk+fillC*nf+coreC*nc);
    if(uMode==2) diff*=0.35;                        // metals: weak diffuse

    vec3 specC=(uMode==2)?albedo:vec3(1.0);
    float sk=pow(max(dot(N,normalize(Lk+V)),0.0),uShin);
    float sc=pow(max(dot(N,normalize(Lc+V)),0.0),uShin);
    float sf=pow(max(dot(N,normalize(Lf+V)),0.0),uShin*0.5);
    vec3 col=diff+specC*spec*(keyC*sk*nk+coreC*sc*nc*1.5+fillC*sf*0.6);

    float fres=pow(1.0-max(dot(N,V),0.0),4.0);
    col+=skyBase(reflect(-V,N))*refl*(0.15+0.85*fres)*(uMode==2?albedo+0.2:vec3(1.0));
    col+=vec3(0.45,0.30,0.95)*pow(1.0-max(dot(N,V),0.0),3.0)*0.18;     // orchid rim
    col+=albedo*uEmit;

    FragColor=vec4(fogIt(col,V,dist),1.0);
}
)GLSL";

static const char* SKY_VS = R"GLSL(
uniform mat4 uInvVP; uniform vec3 uCam;
out vec3 vDir;
void main(){
    vec2 p=vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));
    vec2 ndc=p*2.0-1.0;
    gl_Position=vec4(ndc,1.0,1.0);
    vec4 w=uInvVP*vec4(ndc,1.0,1.0);
    vDir=w.xyz/w.w-uCam;
}
)GLSL";

static const char* SKY_FS = R"GLSL(
in vec3 vDir;
uniform float uTime;
uniform vec3 uNeb0, uNeb1;
out vec4 FragColor;
void main(){
    vec3 d=normalize(vDir);
    vec3 c=skyBase(d);
    float n=fbm(d*3.2+vec3(0.0,0.0,uTime*0.008));
    float n2=fbm(d*6.5+n*2.0);
    float m=smoothstep(0.46,0.86,n)*smoothstep(-0.05,0.45,d.y);
    c+=mix(uNeb0,uNeb1,n2)*m*0.50;
    FragColor=vec4(c,1.0);
}
)GLSL";

static const char* POINT_VS = R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=2) in vec3 aCol;
layout(location=3) in vec4 aAux;     // size, phase, alpha
uniform mat4 uViewProj, uModel;
uniform float uTime, uWorldSize, uPx, uPointScale, uTwinkle, uSwirl;
out vec3 vCol; out float vA;
void main(){
    vec3 p=aPos;
    if(uSwirl>0.5){
        float r=length(p.xz);
        float a=uTime*uSwirl/(0.7+r*0.4);
        p.xz=vec2(cos(a)*p.x-sin(a)*p.z, sin(a)*p.x+cos(a)*p.z);
    }
    vec4 cp=uViewProj*uModel*vec4(p,1.0);
    gl_Position=cp;
    float tw=1.0+uTwinkle*(0.45*sin(uTime*2.3+aAux.y*40.0));
    float px=mix(aAux.x*uPx, aAux.x*uPointScale/max(cp.w,0.1), uWorldSize);
    gl_PointSize=clamp(px,1.0,96.0);
    vCol=aCol*tw; vA=aAux.z;
}
)GLSL";

static const char* POINT_FS = R"GLSL(
in vec3 vCol; in float vA;
out vec4 FragColor;
void main(){
    vec2 q=gl_PointCoord*2.0-1.0; float d=dot(q,q);
    if(d>1.0) discard;
    float a=exp(-d*3.5);
    FragColor=vec4(vCol*a*vA,1.0);
}
)GLSL";

static const char* BILL_VS = R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=2) in vec3 aCol;
layout(location=3) in vec4 aAux;     // cornerX, cornerY, size, alpha
uniform mat4 uViewProj; uniform vec3 uRight, uUp; uniform float uTime;
out vec3 vCol; out vec2 vUV; out float vA;
void main(){
    vec3 p=aPos; p.x+=sin(uTime*0.03+aPos.z*0.05)*1.5;
    p+=(uRight*aAux.x+uUp*aAux.y)*aAux.z*0.5;
    gl_Position=uViewProj*vec4(p,1.0);
    vCol=pow(aCol,vec3(2.2)); vUV=aAux.xy; vA=aAux.w;
}
)GLSL";

static const char* BILL_FS = R"GLSL(
in vec3 vCol; in vec2 vUV; in float vA;
uniform vec3 uCloudTint;
out vec4 FragColor;
void main(){
    float d=dot(vUV,vUV);
    float a=exp(-d*2.8)*vA*(1.0-smoothstep(0.75,1.0,d));
    FragColor=vec4(vCol*uCloudTint,a);
}
)GLSL";

static const char* HUD_VS = R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=2) in vec3 aCol;
layout(location=3) in vec4 aAux;
uniform vec2 uRes;
out vec4 vC;
void main(){ gl_Position=vec4(aPos.x/uRes.x*2.0-1.0, 1.0-aPos.y/uRes.y*2.0, 0.0, 1.0); vC=vec4(aCol,aAux.x); }
)GLSL";
static const char* HUD_FS = R"GLSL(
in vec4 vC; out vec4 FragColor;
void main(){ FragColor=vC; }
)GLSL";

static const char* FS_VS = R"GLSL(
out vec2 vUV;
void main(){
    vec2 p=vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));
    vUV=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)GLSL";

static const char* BRIGHT_FS = R"GLSL(
in vec2 vUV; uniform sampler2D uTex; uniform float uThreshold;
out vec4 FragColor;
void main(){
    vec3 c=min(texture(uTex,vUV).rgb,vec3(24.0));
    float l=max(max(c.r,c.g),c.b);
    float k=max(l-uThreshold,0.0)/max(l,1e-4);
    FragColor=vec4(c*k,1.0);
}
)GLSL";

static const char* BLUR_FS = R"GLSL(
in vec2 vUV; uniform sampler2D uTex; uniform vec2 uStep;
out vec4 FragColor;
void main(){
    const float w[5]=float[](0.227027,0.1945946,0.1216216,0.054054,0.016216);
    vec3 r=texture(uTex,vUV).rgb*w[0];
    for(int i=1;i<5;i++){
        r+=texture(uTex,vUV+uStep*float(i)).rgb*w[i];
        r+=texture(uTex,vUV-uStep*float(i)).rgb*w[i];
    }
    FragColor=vec4(r,1.0);
}
)GLSL";

static const char* COMP_FS = R"GLSL(
in vec2 vUV; uniform sampler2D uScene, uBloom; uniform float uBloomK, uFade, uTime, uPortal; uniform vec3 uPortalCol;
out vec4 FragColor;
vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }
void main(){
    vec2 dv=vUV-0.5;
    float r0=length(dv);
    vec2 uv=vUV;
    if(uPortal>0.0){
        float ang=uPortal*2.6*(1.0-smoothstep(0.0,0.8,r0));
        float cs=cos(ang), sn=sin(ang);
        vec2 rd=vec2(cs*dv.x-sn*dv.y, sn*dv.x+cs*dv.y);
        uv=0.5+rd*(1.0-0.25*uPortal)+normalize(dv+vec2(1e-5))*sin(r0*42.0-uTime*6.0)*0.007*uPortal;
    }
    float ca=dot(dv,dv)*0.012;
    vec3 c=vec3(texture(uScene,uv-dv*ca).r, texture(uScene,uv).g, texture(uScene,uv+dv*ca).b);
    vec3 b=texture(uBloom,uv).rgb;
    c+=b*uBloomK;
    c=aces(c*0.95);
    c*=mix(vec3(1.02,0.97,1.06),vec3(1.0),0.5);                     // cool shadows
    float vig=smoothstep(1.05,0.25,length(dv*vec2(1.0,1.08))*1.25);
    c*=mix(0.55,1.0,vig);
    c=pow(c,vec3(1.0/2.2));
    float g=fract(sin(dot(gl_FragCoord.xy+uTime*60.0,vec2(12.9898,78.233)))*43758.5453);
    c+=(g-0.5)*0.022;
    if(uPortal>0.0){
        float cover=smoothstep(0.0,1.0,uPortal*2.0-r0*1.4);
        float a2=atan(dv.y,dv.x);
        float sw=0.5+0.5*sin(a2*5.0+r0*18.0-uTime*4.0);
        vec3 veil=uPortalCol*(0.35+0.65*sw)+vec3(pow(max(1.0-r0*1.6,0.0),2.0)*0.55);
        c=mix(c,veil,cover);
    }
    FragColor=vec4(c*uFade,1.0);
}
)GLSL";

static GLuint compileShader(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, 4096, nullptr, log);
        std::cerr << "Shader compile error:\n" << log << '\n';
        std::exit(1);
    }
    return s;
}

struct Prog {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> cache;
    void build(const std::string& vs, const std::string& fs, bool commonInVS = false, bool commonInFS = true) {
        const std::string head = "#version 330 core\n";
        GLuint v = compileShader(GL_VERTEX_SHADER, head + (commonInVS ? GLSL_COMMON : "") + vs);
        GLuint f = compileShader(GL_FRAGMENT_SHADER, head + (commonInFS ? GLSL_COMMON : "") + fs);
        id = glCreateProgram();
        glAttachShader(id, v);
        glAttachShader(id, f);
        glLinkProgram(id);
        glDeleteShader(v);
        glDeleteShader(f);
        GLint ok = 0;
        glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetProgramInfoLog(id, 4096, nullptr, log);
            std::cerr << "Program link error:\n" << log << '\n';
            std::exit(1);
        }
    }
    void use() const { glUseProgram(id); }
    GLint u(const char* n) {
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        return cache[n] = glGetUniformLocation(id, n);
    }
    void f1(const char* n, float x) { glUniform1f(u(n), x); }
    void i1(const char* n, int x) { glUniform1i(u(n), x); }
    void v2(const char* n, vec2 x) { glUniform2fv(u(n), 1, glm::value_ptr(x)); }
    void v3(const char* n, vec3 x) { glUniform3fv(u(n), 1, glm::value_ptr(x)); }
    void m4(const char* n, const mat4& x) { glUniformMatrix4fv(u(n), 1, GL_FALSE, glm::value_ptr(x)); }
};

// ----------------------------------------------------------------------------
//  Procedural helpers (CPU noise + RNG)
// ----------------------------------------------------------------------------
struct Rng {
    unsigned s;
    explicit Rng(unsigned seed) : s(seed) {}
    float f() { s = 1664525u * s + 1013904223u; return float((s >> 8) & 0xffffffu) / float(0x1000000u); }
    float r(float a, float b) { return a + (b - a) * f(); }
};
static float hash3(int x, int y, int z) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0xffffffu) / float(0xffffffu);
}
static float vnoise(vec3 p) {
    vec3 i = glm::floor(p), f = p - i;
    f = f * f * (3.0f - 2.0f * f);
    int x = (int)i.x, y = (int)i.y, z = (int)i.z;
    auto h = [&](int a, int b, int c) { return hash3(x + a, y + b, z + c); };
    float x00 = glm::mix(h(0, 0, 0), h(1, 0, 0), f.x), x10 = glm::mix(h(0, 1, 0), h(1, 1, 0), f.x);
    float x01 = glm::mix(h(0, 0, 1), h(1, 0, 1), f.x), x11 = glm::mix(h(0, 1, 1), h(1, 1, 1), f.x);
    return glm::mix(glm::mix(x00, x10, f.y), glm::mix(x01, x11, f.y), f.z);
}
static float fbm3(vec3 p, int oct = 4) {
    float a = 0.5f, s = 0.0f;
    for (int i = 0; i < oct; ++i) { s += a * vnoise(p); p = p * 2.03f + vec3(17.1f, 3.7f, 9.2f); a *= 0.5f; }
    return s;
}
static float sstep(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }

// ----------------------------------------------------------------------------
//  Geometry builders (all return vertex lists; combine with append())
// ----------------------------------------------------------------------------
static void tri(VList& o, const Vertex& a, const Vertex& b, const Vertex& c) { o.push_back(a); o.push_back(b); o.push_back(c); }

static void append(VList& dst, const VList& src, const mat4& M = mat4(1.0f)) {
    mat3 N(M);
    for (Vertex v : src) {
        v.pos = vec3(M * vec4(v.pos, 1.0f));
        v.nrm = glm::normalize(N * v.nrm);
        dst.push_back(v);
    }
}

// Generic parametric surface; normals by finite differences.
static void surface(VList& out, const std::function<vec3(float, float)>& f,
                    const std::function<vec3(float, float)>& color, int nu, int nv) {
    const float e = 2e-3f;
    auto nrm = [&](float u, float v) {
        float uc = std::clamp(u, e, 1 - e), vc = std::clamp(v, e, 1 - e);
        vec3 du = f(uc + e, vc) - f(uc - e, vc), dv = f(uc, vc + e) - f(uc, vc - e);
        vec3 n = glm::cross(du, dv);
        float l = glm::length(n);
        return l > 1e-14f ? n / l : vec3(0, 1, 0);
    };
    std::vector<Vertex> g(size_t(nu + 1) * (nv + 1));
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i <= nu; ++i) {
            float u = float(i) / nu, v = float(j) / nv;
            g[size_t(j) * (nu + 1) + i] = Vertex{f(u, v), nrm(u, v), color(u, v)};
        }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const Vertex& a = g[size_t(j) * (nu + 1) + i];
            const Vertex& b = g[size_t(j) * (nu + 1) + i + 1];
            const Vertex& c = g[size_t(j + 1) * (nu + 1) + i + 1];
            const Vertex& d = g[size_t(j + 1) * (nu + 1) + i];
            tri(out, a, b, c);
            tri(out, a, c, d);
        }
}

static vec3 dirOf(float u, float v) {
    float lon = 2 * PI * u, lat = -PI / 2 + PI * v;
    return {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
}

static VList planetMesh(float R, float relief, float freq, float seed,
                        const std::function<vec3(const vec3&, float)>& colorFn, int nu = 128, int nv = 64) {
    auto height = [&](const vec3& d) { return fbm3(d * freq + vec3(seed), 5); };
    VList out;
    surface(out,
            [&](float u, float v) { vec3 d = dirOf(u, v); return d * R * (1.0f + relief * (height(d) - 0.47f)); },
            [&](float u, float v) { vec3 d = dirOf(u, v); return colorFn(d, height(d)); }, nu, nv);
    return out;
}
static VList sphereMesh(float R, vec3 color, int nu = 48, int nv = 24) {
    VList out;
    surface(out, [&](float u, float v) { return dirOf(u, v) * R; }, [&](float, float) { return color; }, nu, nv);
    return out;
}

struct Prof { float r, y; vec3 c; };
static void lathe(VList& out, const std::vector<Prof>& p, int segs) {
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        const Prof &a = p[i], &b = p[i + 1];
        vec2 d(b.r - a.r, b.y - a.y);
        float L = glm::length(d);
        if (L < 1e-6f) continue;
        vec2 n2(d.y / L, -d.x / L);
        for (int j = 0; j < segs; ++j) {
            float a0 = 2 * PI * j / segs, a1 = 2 * PI * (j + 1) / segs;
            auto P = [&](const Prof& q, float an) { return vec3(q.r * std::cos(an), q.y, q.r * std::sin(an)); };
            auto Nr = [&](float an) { return vec3(n2.x * std::cos(an), n2.y, n2.x * std::sin(an)); };
            Vertex v00{P(a, a0), Nr(a0), a.c}, v10{P(a, a1), Nr(a1), a.c};
            Vertex v11{P(b, a1), Nr(a1), b.c}, v01{P(b, a0), Nr(a0), b.c};
            tri(out, v00, v10, v11);
            tri(out, v00, v11, v01);
        }
    }
}

static void tube(VList& out, const std::vector<vec3>& path, float r0, float r1, int sides, vec3 col, bool closed) {
    int n = (int)path.size();
    if (n < 2) return;
    std::vector<vec3> T(n), N(n), B(n);
    for (int i = 0; i < n; ++i) {
        vec3 prev = closed ? path[(i + n - 1) % n] : path[std::max(i - 1, 0)];
        vec3 next = closed ? path[(i + 1) % n] : path[std::min(i + 1, n - 1)];
        T[i] = glm::normalize(next - prev);
    }
    vec3 helper = std::fabs(T[0].y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    N[0] = glm::normalize(glm::cross(T[0], helper));
    B[0] = glm::cross(T[0], N[0]);
    for (int i = 1; i < n; ++i) {
        vec3 v = N[i - 1] - T[i] * glm::dot(N[i - 1], T[i]);
        N[i] = glm::length(v) > 1e-5f ? glm::normalize(v) : N[i - 1];
        B[i] = glm::cross(T[i], N[i]);
    }
    auto ringV = [&](int i, int k) {
        float an = 2 * PI * k / sides;
        vec3 d = std::cos(an) * N[i] + std::sin(an) * B[i];
        float r = glm::mix(r0, r1, float(i) / float(n - 1));
        return Vertex{path[i] + d * r, d, col};
    };
    int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; ++i) {
        int j = (i + 1) % n;
        for (int k = 0; k < sides; ++k) {
            int k2 = (k + 1) % sides;
            Vertex a = ringV(i, k), b = ringV(i, k2), c = ringV(j, k2), d = ringV(j, k);
            tri(out, a, b, c);
            tri(out, a, c, d);
        }
    }
}

static void box(VList& out, vec3 h, vec3 col, const mat4& M) {
    static const vec3 Ns[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    VList tmp;
    for (int f = 0; f < 6; ++f) {
        vec3 n = Ns[f];
        vec3 a = f < 2 ? vec3(0, 1, 0) : (f < 4 ? vec3(0, 0, 1) : vec3(1, 0, 0));
        vec3 b = glm::cross(n, a);
        auto C = [&](float s1, float s2) { return Vertex{(n + s1 * a + s2 * b) * h, n, col}; };
        tri(tmp, C(-1, -1), C(1, -1), C(1, 1));
        tri(tmp, C(-1, -1), C(1, 1), C(-1, 1));
    }
    append(out, tmp, M);
}

static VList rockSurface(float R, float depth, float seed, vec3 cTop, vec3 cBot, int nu = 96, int nv = 44) {
    auto shape = [&](float u, float v) {
        float an = 2 * PI * u;
        float r = R * (1.0f - std::pow(v, 1.5f));
        float disp = (fbm3(vec3(std::cos(an) * 1.5f, v * 2.6f, std::sin(an) * 1.5f) + vec3(seed), 4) - 0.47f) * 1.7f;
        float w = sstep(0.0f, 0.18f, v) * (1.0f - v);
        r *= 1.0f + disp * w * 0.55f;
        return vec3(r * std::cos(an), -depth * v + disp * w * 0.6f, r * std::sin(an));
    };
    VList out;
    surface(out, shape, [&](float u, float v) {
        float an = 2 * PI * u;
        float n = fbm3(vec3(std::cos(an) * 3.0f, v * 5.0f, std::sin(an) * 3.0f) + vec3(seed * 1.7f), 4);
        return glm::mix(cTop, cBot, v) * (0.65f + 0.9f * n);
    }, nu, nv);
    return out;
}

static std::vector<vec3> archPath(float hw, float hs, int seg = 28) {
    std::vector<vec3> p;
    for (int k = 0; k <= 4; ++k) p.push_back({-hw, hs * k / 4.0f, 0});
    for (int k = 1; k < seg; ++k) { float a = PI - PI * k / seg; p.push_back({hw * std::cos(a), hs + hw * std::sin(a), 0}); }
    for (int k = 0; k <= 4; ++k) p.push_back({hw, hs * (1.0f - k / 4.0f), 0});
    return p;
}

static void crystal(VList& out, vec3 pos, float h, float r, vec3 col, float tiltX, float tiltZ) {
    VList c;
    lathe(c, {{0, 0, col * 0.5f}, {r, 0.22f * h, col}, {r, 0.62f * h, col * 1.1f}, {0, h, col * 1.35f}}, 6);
    mat4 M = glm::translate(mat4(1.0f), pos) * glm::rotate(mat4(1.0f), tiltX, vec3(1, 0, 0)) * glm::rotate(mat4(1.0f), tiltZ, vec3(0, 0, 1));
    append(out, c, M);
}

// ----------------------------------------------------------------------------
//  Application state / callbacks
// ----------------------------------------------------------------------------
// Application modes (transition state machine from the blueprint)
enum Mode { M_HUB, M_APPROACH, M_CROSS, M_ARRIVE, M_EXPLORE, M_RETURN };

struct State {
    float yaw = 0.55f, pitch = 0.30f, distance = 19.0f;
    float atlasTarget = 0.0f, atlasAngle = 0.0f;
    bool dragging = false, fullscreen = false, autoOrbit = false, bloom = true;
    bool skipIntro = false, burst = false, shot = false;
    double lastX = 0, lastY = 0, curX = 0, curY = 0;
    int winX = 100, winY = 100, winW = 1280, winH = 720;
    int mode = M_HUB;
    // one-shot input events, consumed by the main loop
    bool clickL = false, enter = false, esc = false, space = false, back = false, interact = false, focus = false, reset = false;
    int tab = 0;
    float scroll = 0.0f, lookDX = 0.0f, lookDY = 0.0f;
};

static void onClick(GLFWwindow* w, int button, int action, int) {
    auto* s = (State*)glfwGetWindowUserPointer(w);
    if (action == GLFW_PRESS) s->skipIntro = true;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) s->clickL = true;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        s->dragging = action == GLFW_PRESS;
        glfwGetCursorPos(w, &s->lastX, &s->lastY);
    }
}
static void onMove(GLFWwindow* w, double x, double y) {
    auto* s = (State*)glfwGetWindowUserPointer(w);
    s->curX = x;
    s->curY = y;
    if (s->dragging) {
        if (s->mode == M_HUB) {
            s->yaw += float(x - s->lastX) * 0.006f;
            s->pitch = std::clamp(s->pitch + float(y - s->lastY) * 0.006f, -1.2f, 1.2f);
        } else if (s->mode == M_EXPLORE) {
            s->lookDX += float(x - s->lastX);
            s->lookDY += float(y - s->lastY);
        }
    }
    s->lastX = x;
    s->lastY = y;
}
static void onScroll(GLFWwindow* w, double, double dy) {
    auto* s = (State*)glfwGetWindowUserPointer(w);
    s->scroll += float(dy);
}
static void onKey(GLFWwindow* w, int k, int, int action, int mods) {
    if (action != GLFW_PRESS) return;
    auto* s = (State*)glfwGetWindowUserPointer(w);
    s->skipIntro = true;
    switch (k) {
        case GLFW_KEY_ESCAPE: s->esc = true; break;
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: s->enter = true; break;
        case GLFW_KEY_TAB: s->tab = (mods & GLFW_MOD_SHIFT) ? -1 : 1; break;
        case GLFW_KEY_BACKSPACE: s->back = true; break;
        case GLFW_KEY_SPACE: s->space = true; break;
        case GLFW_KEY_E: s->interact = true; break;
        case GLFW_KEY_F: s->focus = true; break;
        case GLFW_KEY_R: s->reset = true; break;
        case GLFW_KEY_B: s->bloom = !s->bloom; break;
        case GLFW_KEY_P: s->shot = true; break;
        case GLFW_KEY_F11:
            s->fullscreen = !s->fullscreen;
            if (s->fullscreen) {
                glfwGetWindowPos(w, &s->winX, &s->winY);
                glfwGetWindowSize(w, &s->winW, &s->winH);
                GLFWmonitor* mon = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(mon);
                glfwSetWindowMonitor(w, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
            } else {
                glfwSetWindowMonitor(w, nullptr, s->winX, s->winY, s->winW, s->winH, 0);
            }
            break;
        default: break;
    }
}

// ----------------------------------------------------------------------------
//  Ray picking + tiny bitmap-font HUD
// ----------------------------------------------------------------------------
static bool raySphere(const vec3& O, const vec3& D, const vec3& C, float r, float& t) {
    vec3 oc = O - C;
    float b = glm::dot(oc, D), c = glm::dot(oc, oc) - r * r, disc = b * b - c;
    if (disc < 0.0f) return false;
    float sq = std::sqrt(disc), t0 = -b - sq, t1 = -b + sq;
    t = t0 > 0.0f ? t0 : t1;
    return t > 0.0f;
}

struct Glyph { char c; unsigned char r[7]; };
static const Glyph FONT[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'J', {7, 2, 2, 2, 2, 18, 12}},      {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},      {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},  {'W', {17, 17, 17, 21, 21, 27, 17}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},      {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},     {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 30, 1, 1, 17, 14}},
    {'6', {6, 8, 16, 30, 17, 17, 14}},   {'7', {31, 1, 2, 4, 8, 8, 8}},       {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 2, 12}},   {'-', {0, 0, 0, 31, 0, 0, 0}},       {':', {0, 4, 0, 0, 0, 4, 0}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},      {'/', {1, 2, 2, 4, 8, 8, 16}},       {'|', {4, 4, 4, 4, 4, 4, 4}},
    {'[', {14, 8, 8, 8, 8, 8, 14}},      {']', {14, 2, 2, 2, 2, 2, 14}},      {'!', {4, 4, 4, 4, 4, 0, 4}},
    {'?', {14, 17, 1, 2, 4, 0, 4}},      {'+', {0, 4, 4, 31, 4, 4, 0}},       {',', {0, 0, 0, 0, 12, 4, 8}}};

static float textWidth(const std::string& s, float px) { return s.empty() ? 0.0f : (s.size() * 6 - 1) * px; }
static void hudRect(VList& o, float x, float y, float w, float h, vec3 c, float a) {
    Vertex v[4];
    const vec2 p[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    for (int i = 0; i < 4; ++i) { v[i].pos = vec3(p[i], 0); v[i].col = c; v[i].aux = vec4(a, 0, 0, 0); }
    tri(o, v[0], v[1], v[2]);
    tri(o, v[0], v[2], v[3]);
}
static void hudText(VList& o, std::string s, float x, float y, float px, vec3 c, float a, bool centered = false, bool shadow = true) {
    for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
    if (centered) x -= textWidth(s, px) * 0.5f;
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
        float ox = pass == 0 ? px * 0.8f : 0.0f, oy = pass == 0 ? px * 0.8f : 0.0f;
        vec3 col = pass == 0 ? vec3(0.02f, 0.01f, 0.06f) : c;
        float al = pass == 0 ? a * 0.8f : a;
        float cx = x;
        for (char ch : s) {
            const unsigned char* rows = nullptr;
            for (const Glyph& g : FONT) if (g.c == ch) { rows = g.r; break; }
            if (rows) {
                for (int r = 0; r < 7; ++r) {
                    int run = -1;
                    for (int cI = 0; cI <= 5; ++cI) {
                        bool on = cI < 5 && (rows[r] & (16 >> cI));
                        if (on && run < 0) run = cI;
                        if (!on && run >= 0) { hudRect(o, cx + ox + run * px, y + oy + r * px, (cI - run) * px, px, col, al); run = -1; }
                    }
                }
            }
            cx += 6 * px;
        }
    }
}

// ----------------------------------------------------------------------------
//  Render targets (HDR + MSAA + bloom chain)
// ----------------------------------------------------------------------------
struct Targets {
    int w = 0, h = 0, bw = 0, bh = 0;
    GLuint msFbo = 0, msColor = 0, msDepth = 0, resFbo = 0, resTex = 0;
    GLuint bFbo[2] = {0, 0}, bTex[2] = {0, 0};

    static GLuint makeTex(int w, int h) {
        GLuint t;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return t;
    }
    void destroy() {
        if (msFbo) glDeleteFramebuffers(1, &msFbo);
        if (msColor) glDeleteRenderbuffers(1, &msColor);
        if (msDepth) glDeleteRenderbuffers(1, &msDepth);
        if (resFbo) glDeleteFramebuffers(1, &resFbo);
        if (resTex) glDeleteTextures(1, &resTex);
        for (int i = 0; i < 2; ++i) {
            if (bFbo[i]) glDeleteFramebuffers(1, &bFbo[i]);
            if (bTex[i]) glDeleteTextures(1, &bTex[i]);
        }
        msFbo = msColor = msDepth = resFbo = resTex = 0;
        bFbo[0] = bFbo[1] = bTex[0] = bTex[1] = 0;
    }
    void create(int W, int H) {
        destroy();
        w = W; h = H; bw = std::max(W / 2, 1); bh = std::max(H / 2, 1);
        GLint maxS = 4;
        glGetIntegerv(GL_MAX_SAMPLES, &maxS);
        int samples = std::min(4, (int)maxS);
        glGenRenderbuffers(1, &msColor);
        glBindRenderbuffer(GL_RENDERBUFFER, msColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, w, h);
        glGenRenderbuffers(1, &msDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
        glGenFramebuffers(1, &msFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::cerr << "MSAA framebuffer incomplete\n";

        resTex = makeTex(w, h);
        glGenFramebuffers(1, &resFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, resFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::cerr << "Resolve framebuffer incomplete\n";

        for (int i = 0; i < 2; ++i) {
            bTex[i] = makeTex(bw, bh);
            glGenFramebuffers(1, &bFbo[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, bFbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bTex[i], 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

static void saveTGA(const char* path, int w, int h) {
    std::vector<unsigned char> px(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    unsigned char hdr[18] = {0};
    hdr[2] = 2; hdr[12] = w & 255; hdr[13] = w >> 8; hdr[14] = h & 255; hdr[15] = h >> 8; hdr[16] = 24;
    std::fwrite(hdr, 1, 18, f);
    std::fwrite(px.data(), 1, px.size(), f);
    std::fclose(f);
    std::cout << "Saved screenshot: " << path << '\n';
}

// ----------------------------------------------------------------------------
//  Materials
// ----------------------------------------------------------------------------
struct Mat {
    int mode = 0;
    float spec = 0.2f, shin = 32.0f, emit = 0.0f, refl = 0.0f, flap = 0.0f;
    vec3 tint{1.0f};
    float fAmp = 0.42f, fLen = 2.9f, fSpd = 2.2f;
};
static Mat M_MARBLE{1, 0.55f, 70, 0.0f, 0.22f};
static Mat M_STONE{0, 0.08f, 10, 0.0f, 0.0f};
static Mat M_BRASS{2, 1.0f, 90, 0.0f, 0.65f};
static Mat M_INLAY{2, 1.2f, 120, 0.40f, 0.5f};
static Mat M_PLANET{0, 0.40f, 50, 0.0f, 0.06f};
static Mat M_CRYSTAL{0, 1.0f, 110, 1.15f, 0.55f};
static Mat M_MANTA{0, 0.55f, 60, 0.22f, 0.12f, 1.0f};
static Mat emissive(float e, vec3 tint = vec3(1.0f)) { Mat m; m.mode = 3; m.emit = e; m.tint = tint; return m; }
static Mat glass(float e, vec3 tint) { Mat m; m.mode = 4; m.emit = e; m.tint = tint; return m; }
static Mat portalMat(vec3 tint) { Mat m; m.mode = 6; m.tint = tint; return m; }

struct Particle { vec3 p, v, c; float life, maxLife, size; float drag = 0.55f; };

// Sky / atmosphere per environment: 0 = Observatory, 1..4 = the four worlds
struct Palette { vec3 h, l, m, t, sun, neb0, neb1, cloud, coreCol; };
static const Palette PAL[5] = {
    {{1.0f, 0.46f, 0.38f}, {0.30f, 0.10f, 0.36f}, {0.055f, 0.028f, 0.20f}, {0.006f, 0.005f, 0.04f}, {-0.55f, 0.10f, -0.83f}, {0.55f, 0.18f, 0.75f}, {0.18f, 0.75f, 0.65f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.66f, 0.30f}},
    {{0.35f, 1.0f, 0.70f}, {0.06f, 0.28f, 0.32f}, {0.03f, 0.07f, 0.24f}, {0.004f, 0.008f, 0.05f}, {0.30f, 0.20f, -0.90f}, {0.20f, 0.80f, 0.60f}, {0.60f, 0.30f, 0.90f}, {0.55f, 1.0f, 0.90f}, {0.45f, 1.0f, 0.75f}},
    {{1.0f, 0.42f, 0.12f}, {0.55f, 0.14f, 0.18f}, {0.18f, 0.05f, 0.24f}, {0.03f, 0.01f, 0.10f}, {-0.20f, 0.02f, -1.0f}, {0.90f, 0.35f, 0.20f}, {0.60f, 0.20f, 0.50f}, {1.3f, 0.75f, 0.55f}, {1.0f, 0.55f, 0.20f}},
    {{0.85f, 0.55f, 1.0f}, {0.28f, 0.12f, 0.50f}, {0.07f, 0.035f, 0.27f}, {0.01f, 0.005f, 0.07f}, {0.50f, 0.25f, -0.80f}, {0.60f, 0.30f, 0.90f}, {0.90f, 0.70f, 0.30f}, {0.95f, 0.75f, 1.2f}, {0.85f, 0.60f, 1.0f}},
    {{0.55f, 0.80f, 1.0f}, {0.10f, 0.18f, 0.42f}, {0.035f, 0.055f, 0.24f}, {0.005f, 0.010f, 0.06f}, {0.0f, 0.60f, -0.80f}, {0.30f, 0.60f, 1.0f}, {0.80f, 0.80f, 1.0f}, {0.65f, 0.85f, 1.3f}, {0.55f, 0.80f, 1.0f}}};

static VList gearMesh(int teeth, vec3 col) {
    VList g;
    std::vector<vec3> c;
    for (int i = 0; i < 64; ++i) c.push_back({0.78f * std::cos(2 * PI * i / 64), 0.78f * std::sin(2 * PI * i / 64), 0});
    tube(g, c, 0.09f, 0.09f, 8, col, true);
    for (int i = 0; i < teeth; ++i) {
        float a = 2 * PI * i / teeth;
        box(g, vec3(0.14f, 0.10f, 0.10f), col * 1.05f, glm::translate(mat4(1.0f), vec3(0.92f * std::cos(a), 0.92f * std::sin(a), 0)) * glm::rotate(mat4(1.0f), a, vec3(0, 0, 1)));
    }
    for (int i = 0; i < 3; ++i) box(g, vec3(0.78f, 0.05f, 0.05f), col * 0.9f, glm::rotate(mat4(1.0f), PI * i / 3, vec3(0, 0, 1)));
    VList hub;
    lathe(hub, {{0, -0.12f, col}, {0.22f, -0.12f, col}, {0.22f, 0.12f, col}, {0, 0.12f, col}}, 16);
    append(g, hub, glm::rotate(mat4(1.0f), PI / 2, vec3(1, 0, 0)));
    return g;
}

// ============================================================================
int main(int argc, char** argv) {
    std::string shotPath;
    int shotFrames = 40, startWorld = -1, startSelect = -1, autoEnter = -1;
    bool startAct = false;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shotPath = argv[++i];
        else if (!std::strcmp(argv[i], "--world") && i + 1 < argc) startWorld = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--act")) startAct = true;
        else if (!std::strcmp(argv[i], "--enter") && i + 1 < argc) autoEnter = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--select") && i + 1 < argc) startSelect = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) shotFrames = std::atoi(argv[++i]);

    if (!glfwInit()) { std::cerr << "GLFW initialization failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* w = glfwCreateWindow(1280, 720, "THE ATLAS OF IMPOSSIBLE WORLDS | Prototype", nullptr, nullptr);
    if (!w) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(w);
    if (!gladLoadGL(glfwGetProcAddress)) { std::cerr << "GLAD failed\n"; glfwDestroyWindow(w); glfwTerminate(); return 1; }
    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n";
    glfwSwapInterval(1);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // ------------------------------------------------------------ programs
    Prog scene, sky, points, bill, bright, blur, comp, hud;
    scene.build(SCENE_VS, SCENE_FS, false, true);
    sky.build(SKY_VS, SKY_FS, false, true);
    points.build(POINT_VS, POINT_FS, false, false);
    bill.build(BILL_VS, BILL_FS, false, false);
    bright.build(FS_VS, BRIGHT_FS, false, false);
    blur.build(FS_VS, BLUR_FS, false, false);
    comp.build(FS_VS, COMP_FS, false, false);
    hud.build(HUD_VS, HUD_FS, false, false);
    GLuint emptyVao = 0;
    glGenVertexArrays(1, &emptyVao);

    State state;
    if (!shotPath.empty()) state.skipIntro = true;
    glfwSetWindowUserPointer(w, &state);
    glfwSetMouseButtonCallback(w, onClick);
    glfwSetCursorPosCallback(w, onMove);
    glfwSetScrollCallback(w, onScroll);
    glfwSetKeyCallback(w, onKey);

    // ======================================================== BUILD GEOMETRY
    const vec3 GOLD(0.96f, 0.76f, 0.36f), ORCHID(0.81f, 0.57f, 0.93f), MINT(0.55f, 0.95f, 0.82f), ICE(0.55f, 0.85f, 1.0f);
    const float Y0 = DECK_Y;

    // ---- observatory deck (marble): dais steps + floor + rim wall + floating stairs
    VList deckV;
    {
        vec3 top(0.86f, 0.82f, 0.94f), wall(0.60f, 0.56f, 0.78f);
        lathe(deckV, {{0, Y0 + .36f, top}, {2.3f, Y0 + .36f, top}, {2.3f, Y0 + .24f, wall}, {2.3f, Y0 + .24f, top},
                      {2.7f, Y0 + .24f, top}, {2.7f, Y0 + .12f, wall}, {2.7f, Y0 + .12f, top}, {3.1f, Y0 + .12f, top},
                      {3.1f, Y0, wall}, {3.1f, Y0, top}, {7.0f, Y0, top}, {7.25f, Y0 - .04f, top},
                      {7.5f, Y0 - .04f, wall}, {7.5f, Y0 - .40f, wall * 0.8f}}, 128);
        // floating spiral stairway ending in a door
        for (int i = 0; i < 18; ++i) {
            float t = i / 17.0f, an = 3.2f + t * 3.4f;
            vec3 p = vec3(5.6f * std::cos(an), Y0 + 0.7f + t * 5.6f, 5.6f * std::sin(an));
            mat4 M = glm::translate(mat4(1.0f), p) * glm::rotate(mat4(1.0f), -an, vec3(0, 1, 0)) *
                     glm::rotate(mat4(1.0f), 0.06f * std::sin(i * 1.7f), vec3(0, 0, 1));
            box(deckV, vec3(0.75f, 0.075f, 0.36f), top, M);
        }
    }
    // ---- rock underside of the platform + hanging crystals
    VList rockV = rockSurface(7.5f, 9.6f, 3.0f, vec3(0.34f, 0.25f, 0.50f), vec3(0.16f, 0.12f, 0.30f), 120, 56);
    {
        mat4 M = glm::translate(mat4(1.0f), vec3(0, Y0 - 0.4f, 0));
        VList moved;
        append(moved, rockV, M);
        rockV.swap(moved);
    }
    // ---- brass: rim torus, arches, plinths, axis, door frame
    VList brassV, inlayV, crystalV, portalV;
    {
        std::vector<vec3> rim;
        for (int i = 0; i < 180; ++i) rim.push_back({7.5f * std::cos(2 * PI * i / 180), Y0 - 0.04f, 7.5f * std::sin(2 * PI * i / 180)});
        tube(brassV, rim, 0.11f, 0.11f, 10, GOLD * 0.95f, true);
        std::vector<vec3> rim2;
        for (int i = 0; i < 140; ++i) rim2.push_back({3.1f * std::cos(2 * PI * i / 140), Y0 + 0.13f, 3.1f * std::sin(2 * PI * i / 140)});
        tube(brassV, rim2, 0.05f, 0.05f, 8, GOLD, true);
    }
    // arch template (frame + gold inner arc) ------------------------------
    VList archFrame, archGold, keystone;
    {
        tube(archFrame, archPath(1.5f, 2.4f), 0.13f, 0.13f, 12, GOLD * 0.92f, false);
        tube(archGold, archPath(1.22f, 2.4f), 0.045f, 0.045f, 8, GOLD, false);
        box(archFrame, vec3(0.30f, 0.12f, 0.30f), GOLD * 0.8f, glm::translate(mat4(1.0f), vec3(-1.5f, 0.1f, 0)));
        box(archFrame, vec3(0.30f, 0.12f, 0.30f), GOLD * 0.8f, glm::translate(mat4(1.0f), vec3(1.5f, 0.1f, 0)));
        crystal(keystone, vec3(0, 3.95f, 0), 0.55f, 0.16f, MINT, 0, 0);
        // portal veil polygon
        auto path = archPath(1.38f, 2.4f);
        vec3 c0(0, 1.5f, 0);
        for (size_t i = 0; i < path.size(); ++i) {
            vec3 a = path[i], b = path[(i + 1) % path.size()];
            tri(portalV, Vertex{c0, {0, 0, 1}, vec3(1)}, Vertex{a, {0, 0, 1}, vec3(1)}, Vertex{b, {0, 0, 1}, vec3(1)});
        }
    }
    struct Gate { mat4 M; vec3 tint; };
    std::vector<Gate> portals;
    const vec3 worldTint[4] = {{0.42f, 0.89f, 0.74f}, {1.0f, 0.62f, 0.22f}, {0.72f, 0.45f, 1.0f}, {0.50f, 0.80f, 1.0f}};
    const vec3 crystalPal[4] = {ORCHID, MINT, vec3(1.0f, 0.72f, 0.55f), ICE};
    for (int k = 0; k < 8; ++k) {
        float a = k * PI / 4;
        vec3 R(std::cos(a), 0, std::sin(a));
        mat4 M = glm::translate(mat4(1.0f), R * 6.9f + vec3(0, Y0, 0)) * glm::rotate(mat4(1.0f), -(a + PI / 2), vec3(0, 1, 0));
        append(brassV, archFrame, M);
        append(inlayV, archGold, M);
        VList ks;
        crystal(ks, vec3(0, 3.95f, 0), 0.55f, 0.16f, crystalPal[(k / 2) % 4], 0, 0);
        append(crystalV, ks, M);
        if (k % 2 == 0) portals.push_back({M, worldTint[k / 2]});
    }
    {   // stairway door (scaled-down arch)
        float an = 3.2f + 3.4f;
        vec3 p = vec3(5.6f * std::cos(an), Y0 + 0.7f + 5.6f + 0.075f, 5.6f * std::sin(an));
        mat4 M = glm::translate(mat4(1.0f), p) * glm::rotate(mat4(1.0f), -(an + PI / 2), vec3(0, 1, 0)) * glm::scale(mat4(1.0f), vec3(0.5f));
        append(brassV, archFrame, M);
        append(inlayV, archGold, M);
        portals.push_back({M, vec3(1.0f, 0.55f, 0.50f)});
    }
    // axis rod through the Atlas + finials
    {
        std::vector<vec3> rod;
        for (int i = 0; i <= 24; ++i) rod.push_back({0, 2.4f - 3.4f + 6.8f * i / 24.0f, 0});
        tube(brassV, rod, 0.04f, 0.04f, 10, GOLD, false);
        VList fin;
        lathe(fin, {{0, 0, GOLD}, {0.16f, 0.10f, GOLD}, {0.07f, 0.26f, GOLD}, {0.12f, 0.42f, GOLD}, {0, 0.80f, GOLD}}, 20);
        append(brassV, fin, glm::translate(mat4(1.0f), vec3(0, 5.8f, 0)));
        append(brassV, fin, glm::translate(mat4(1.0f), vec3(0, -1.0f, 0)) * glm::rotate(mat4(1.0f), PI, vec3(1, 0, 0)));
    }
    // gold inlay on the floor: concentric bands, radial spokes, ticks
    {
        auto annulus = [&](float r0, float r1, float y) {
            lathe(inlayV, {{r0, y, GOLD}, {r1, y, GOLD}}, 160);
        };
        annulus(6.55f, 6.68f, Y0 + 0.007f);
        annulus(5.00f, 5.05f, Y0 + 0.007f);
        annulus(3.55f, 3.62f, Y0 + 0.007f);
        annulus(7.02f, 7.22f, Y0 + 0.007f);
        for (int i = 0; i < 16; ++i) {
            float a = i * PI / 8;
            mat4 M = glm::rotate(mat4(1.0f), -a, vec3(0, 1, 0));
            box(inlayV, vec3((6.5f - 3.7f) * 0.5f, 0.004f, i % 2 ? 0.018f : 0.035f), GOLD, glm::translate(M, vec3((6.5f + 3.7f) * 0.5f, Y0 + 0.008f, 0)));
        }
        for (int i = 0; i < 96; ++i) {
            float a = i * 2 * PI / 96;
            float len = (i % 4 == 0) ? 0.28f : 0.14f;
            mat4 M = glm::rotate(mat4(1.0f), -a, vec3(0, 1, 0));
            box(inlayV, vec3(len * 0.5f, 0.004f, 0.012f), GOLD, glm::translate(M, vec3(6.75f + len * 0.5f, Y0 + 0.008f, 0)));
        }
        // 8-point compass star around the dais
        for (int i = 0; i < 8; ++i) {
            float a = i * PI / 4 + PI / 8;
            vec3 d(std::cos(a), 0, std::sin(a)), s(-std::sin(a), 0, std::cos(a));
            float L = (i % 2) ? 3.35f : 3.9f;
            vec3 y(0, Y0 + 0.009f, 0);
            tri(inlayV, Vertex{y + d * L, {0, 1, 0}, GOLD}, Vertex{y + d * 3.1f + s * 0.18f, {0, 1, 0}, GOLD}, Vertex{y + d * 3.1f - s * 0.18f, {0, 1, 0}, GOLD});
        }
    }
    // scattered floor crystals
    {
        Rng rg(77);
        for (int i = 0; i < 18; ++i) {
            float a = i * 2 * PI / 18 + rg.r(-0.12f, 0.12f), r = rg.r(5.3f, 6.2f);
            crystal(crystalV, vec3(r * std::cos(a), Y0, r * std::sin(a)), rg.r(0.5f, 1.2f), rg.r(0.08f, 0.15f),
                    crystalPal[i % 4], rg.r(-0.18f, 0.18f), rg.r(-0.18f, 0.18f));
        }
    }

    // ---- Atlas: core, corona, rings (body + ticks + beads), rays
    struct RingSet { Mesh body, ticks, beads; float radius; };
    auto makeRing = [&](float R, vec3 col, int beadsN) {
        RingSet rs; rs.radius = R;
        VList body, ticks, beads;
        std::vector<vec3> c;
        for (int i = 0; i < 200; ++i) c.push_back({R * std::cos(2 * PI * i / 200), R * std::sin(2 * PI * i / 200), 0});
        tube(body, c, 0.05f, 0.05f, 10, col * 0.95f, true);
        VList band;
        lathe(band, {{R - 0.10f, 0, col * 0.8f}, {R + 0.10f, 0, col * 0.8f}}, 200);
        append(body, band, glm::rotate(mat4(1.0f), PI / 2, vec3(1, 0, 0)));
        float Rl = R + 0.12f;
        for (int i = 0; i < 200; ++i) {
            float a = 2 * PI * i / 200, b = 2 * PI * (i + 1) / 200;
            ticks.push_back({{Rl * std::cos(a), Rl * std::sin(a), 0}, {0, 0, 1}, col});
            ticks.push_back({{Rl * std::cos(b), Rl * std::sin(b), 0}, {0, 0, 1}, col});
        }
        for (int i = 0; i < 72; ++i) {
            float a = 2 * PI * i / 72, len = (i % 6 == 0) ? 0.20f : (i % 2 == 0 ? 0.12f : 0.07f);
            ticks.push_back({{Rl * std::cos(a), Rl * std::sin(a), 0}, {0, 0, 1}, col});
            ticks.push_back({{(Rl + len) * std::cos(a), (Rl + len) * std::sin(a), 0}, {0, 0, 1}, col});
        }
        VList orb = sphereMesh(0.10f, col * 1.1f, 20, 10);
        for (int i = 0; i < beadsN; ++i) {
            float a = 2 * PI * i / beadsN;
            append(beads, orb, glm::translate(mat4(1.0f), vec3(R * std::cos(a), R * std::sin(a), 0)));
        }
        rs.body.upload(body, GL_TRIANGLES);
        rs.ticks.upload(ticks, GL_LINES);
        rs.beads.upload(beads, GL_TRIANGLES);
        return rs;
    };
    RingSet outerRing = makeRing(2.2f, GOLD, 8), middleRing = makeRing(1.65f, ORCHID, 6), innerRing = makeRing(1.2f, ICE, 4);

    Mesh deck, rock, brass, inlay, crystals, core, corona, raysM, portalMesh, keystoneUnused;
    deck.upload(deckV, GL_TRIANGLES);
    rock.upload(rockV, GL_TRIANGLES);
    brass.upload(brassV, GL_TRIANGLES);
    inlay.upload(inlayV, GL_TRIANGLES);
    crystals.upload(crystalV, GL_TRIANGLES);
    portalMesh.upload(portalV, GL_TRIANGLES);
    core.upload(sphereMesh(0.9f, vec3(1.0f, 0.78f, 0.40f), 64, 32), GL_TRIANGLES);
    corona.upload(sphereMesh(1.55f, vec3(1.0f, 0.70f, 0.30f), 48, 24), GL_TRIANGLES);
    {
        VList rays;
        Rng rg(404);
        for (int i = 0; i < 180; ++i) {
            float z = rg.r(-1, 1), a = rg.r(0, 2 * PI), s = std::sqrt(1 - z * z);
            vec3 d(s * std::cos(a), z, s * std::sin(a));
            float len = rg.r(0.25f, 1.5f);
            vec3 c = glm::mix(vec3(1.0f, 0.62f, 0.22f), vec3(1.0f, 0.93f, 0.7f), rg.f());
            rays.push_back({d * 0.95f, {0, 1, 0}, c});
            rays.push_back({d * (0.95f + len), {0, 1, 0}, c * 0.15f});
        }
        raysM.upload(rays, GL_LINES);
    }

    // ---- pedestals + worlds + satellites
    Mesh pedestal, halo, moon, cogRing, saturnRing, shell, atmosphere;
    {
        VList p;
        lathe(p, {{0, Y0, GOLD * 0.7f}, {0.75f, Y0, GOLD * 0.7f}, {0.75f, Y0 + .12f, GOLD}, {0.45f, Y0 + .22f, GOLD},
                  {0.13f, Y0 + .42f, GOLD}, {0.13f, Y0 + .64f, GOLD}, {0.38f, Y0 + .78f, GOLD}, {0.52f, Y0 + .88f, GOLD}, {0.20f, Y0 + .92f, GOLD}}, 48);
        pedestal.upload(p, GL_TRIANGLES);
        std::vector<vec3> c;
        for (int i = 0; i < 120; ++i) c.push_back({0.60f * std::cos(2 * PI * i / 120), Y0 + 1.02f, 0.60f * std::sin(2 * PI * i / 120)});
        VList h;
        tube(h, c, 0.028f, 0.028f, 8, vec3(1.0f), true);
        halo.upload(h, GL_TRIANGLES);
    }
    moon.upload(sphereMesh(0.11f, vec3(0.92f, 0.95f, 1.0f), 24, 12), GL_TRIANGLES);
    shell.upload(sphereMesh(0.86f, vec3(1.0f), 48, 24), GL_TRIANGLES);
    {
        VList cog;
        std::vector<vec3> c;
        for (int i = 0; i < 100; ++i) c.push_back({1.05f * std::cos(2 * PI * i / 100), 0, 1.05f * std::sin(2 * PI * i / 100)});
        tube(cog, c, 0.03f, 0.03f, 8, GOLD, true);
        for (int i = 0; i < 24; ++i) {
            float a = 2 * PI * i / 24;
            box(cog, vec3(0.045f, 0.06f, 0.04f), GOLD * 1.05f, glm::translate(mat4(1.0f), vec3(1.05f * std::cos(a), 0, 1.05f * std::sin(a))) * glm::rotate(mat4(1.0f), -a, vec3(0, 1, 0)) * glm::translate(mat4(1.0f), vec3(0.07f, 0, 0)));
        }
        cogRing.upload(cog, GL_TRIANGLES);
        VList sr;
        for (int b = 0; b < 8; ++b) {
            float r0 = 1.0f + b * 0.075f;
            vec3 col = (b % 3 == 0) ? vec3(0.95f, 0.85f, 0.75f) : (b % 3 == 1 ? vec3(0.75f, 0.55f, 0.95f) : vec3(0.55f, 0.35f, 0.85f));
            lathe(sr, {{r0, 0, col}, {r0 + 0.075f, 0, col}}, 120);
        }
        saturnRing.upload(sr, GL_TRIANGLES);
    }
    Mesh planets[4];
    planets[0].upload(planetMesh(0.7f, 0.22f, 2.6f, 5.0f, [](const vec3& d, float h) {      // Garden
        if (std::fabs(d.y) > 0.86f + 0.1f * (h - 0.47f)) return vec3(0.95f, 1.0f, 0.97f);
        if (h < 0.44f) return glm::mix(vec3(0.05f, 0.28f, 0.48f), vec3(0.12f, 0.68f, 0.74f), sstep(0.30f, 0.44f, h));
        if (h < 0.47f) return vec3(0.88f, 0.88f, 0.62f);
        return glm::mix(vec3(0.22f, 0.66f, 0.38f), vec3(0.62f, 0.97f, 0.74f), sstep(0.47f, 0.70f, h));
    }), GL_TRIANGLES);
    planets[1].upload(planetMesh(0.7f, 0.26f, 2.8f, 11.0f, [](const vec3& d, float h) {     // Factory
        vec3 c = glm::mix(vec3(0.30f, 0.12f, 0.07f), vec3(0.85f, 0.48f, 0.22f), sstep(0.30f, 0.65f, h));
        float crack = std::fabs(fbm3(d * 5.5f + vec3(3.0f), 4) - 0.47f);
        return glm::mix(c, vec3(1.0f, 0.62f, 0.10f), sstep(0.025f, 0.0f, crack));
    }), GL_TRIANGLES);
    planets[2].upload(planetMesh(0.7f, 0.04f, 3.0f, 21.0f, [](const vec3& d, float h) {     // Library
        float band = 0.5f + 0.5f * std::sin(d.y * 15.0f + fbm3(d * 3.0f, 3) * 5.0f);
        vec3 c = glm::mix(vec3(0.40f, 0.22f, 0.72f), vec3(0.96f, 0.86f, 0.78f), band);
        return glm::mix(c, vec3(0.80f, 0.55f, 0.95f), sstep(0.5f, 0.7f, h) * 0.5f);
    }), GL_TRIANGLES);
    planets[3].upload(planetMesh(0.7f, 0.20f, 2.4f, 31.0f, [](const vec3& d, float h) {     // Kingdom
        vec3 c = glm::mix(vec3(0.62f, 0.82f, 1.0f), vec3(0.95f, 0.98f, 1.0f), sstep(0.35f, 0.65f, h));
        float ridge = std::fabs(fbm3(d * 6.0f + vec3(9.0f), 4) - 0.47f);
        return glm::mix(c, vec3(0.18f, 0.30f, 0.62f), sstep(0.03f, 0.0f, ridge) * 0.85f);
    }), GL_TRIANGLES);

    // ---- distant floating islands (+ inverted towers)
    Mesh islands[3];
    for (int k = 0; k < 3; ++k) {
        Rng rg(900 + k * 13);
        float R = 3.4f + k * 0.9f, depth = 4.4f + k * 1.4f;
        VList v = rockSurface(R, depth, 40.0f + k * 7, vec3(0.34f, 0.26f, 0.52f), vec3(0.14f, 0.10f, 0.28f), 64, 28);
        lathe(v, {{0, 0.18f, vec3(0.46f, 0.82f, 0.66f)}, {R * 0.92f, 0.16f, vec3(0.52f, 0.86f, 0.70f)}, {R, 0, vec3(0.30f, 0.22f, 0.44f)}}, 64);
        vec3 stone(0.92f, 0.86f, 0.96f);
        lathe(v, {{0.55f, 0.1f, stone}, {0.45f, 2.6f, stone}, {0.62f, 2.7f, GOLD}, {0.0f, 4.4f + k * 0.6f, GOLD}}, 14);
        lathe(v, {{0.60f, -depth + 0.4f, stone}, {0.38f, -depth - 1.4f, stone}, {0.0f, -depth - 3.6f - k * 0.6f, GOLD}}, 14);
        for (int i = 0; i < 4 + k; ++i) {
            float a = rg.r(0, 2 * PI), r = rg.r(0.8f, R * 0.8f);
            crystal(v, vec3(r * std::cos(a), 0.15f, r * std::sin(a)), rg.r(0.5f, 1.0f), 0.12f, crystalPal[(i + k) % 4], rg.r(-.2f, .2f), rg.r(-.2f, .2f));
        }
        islands[k].upload(v, GL_TRIANGLES);
    }
    struct Isle { vec3 pos; int mesh; float scale, bob; };
    const Isle isles[] = {{{-27, 4, -15}, 0, 1.2f, 0.0f}, {{-12, 11, -36}, 1, 1.5f, 1.7f}, {{15, 6, -34}, 2, 1.3f, 3.1f},
                          {{-40, 13, 6}, 2, 1.0f, 4.4f}, {{36, 2, -10}, 1, 1.1f, 5.3f}, {{26, 15, -48}, 0, 1.8f, 2.2f}};

    // ---- manta ray (parametric wing surface, flapped in the vertex shader)
    Mesh manta;
    {
        VList v;
        auto W = [](float t) { return 2.9f * std::pow(std::sin(PI * std::pow(t, 0.62f)), 1.15f); };
        auto make = [&](bool top) {
            auto P = [=](float u01, float t) {
                float u = u01 * 2 - 1;
                float x = u * W(t);
                float z = 1.1f - 2.0f * t - 1.05f * std::fabs(u) * std::sin(PI * t) * (0.4f + 0.6f * t);
                float body = std::pow(std::max(1 - u * u, 0.0f), 1.5f) * std::sin(PI * std::clamp(t * 1.05f, 0.0f, 1.0f));
                float y = (top ? 0.20f : -0.06f) * body - 0.12f * u * u * std::sin(PI * t);
                return vec3(x, y, z);
            };
            auto C = [=](float u01, float t) {
                float u = u01 * 2 - 1;
                if (!top) return glm::mix(vec3(0.95f, 0.97f, 1.0f), vec3(0.85f, 0.80f, 0.98f), std::fabs(u));
                vec3 c = glm::mix(vec3(0.10f, 0.50f, 0.66f), vec3(0.50f, 0.93f, 1.0f), std::fabs(u) * std::fabs(u));
                float spots = sstep(0.62f, 0.70f, fbm3(vec3(u * 4.0f, t * 6.0f, 1.3f), 3));
                return glm::mix(c, vec3(0.92f, 1.0f, 1.0f), spots * 0.8f);
            };
            surface(v, P, C, 40, 40);
        };
        make(true);
        make(false);
        std::vector<vec3> tail;
        for (int i = 0; i <= 18; ++i) tail.push_back({0.10f * std::sin(i * 0.5f), 0.0f, -0.85f - 2.0f * i / 18.0f});
        tube(v, tail, 0.045f, 0.008f, 8, vec3(0.12f, 0.40f, 0.55f), false);
        manta.upload(v, GL_TRIANGLES);
    }

    // ---- stars (GL_POINTS)
    Mesh starfield;
    {
        VList v;
        Rng rg(1977);
        for (int i = 0; i < 2400; ++i) {
            float z = rg.r(-0.12f, 1.0f), a = rg.r(0, 2 * PI), s = std::sqrt(1 - z * z), r = rg.r(45.0f, 120.0f);
            float bri = (rg.f() < 0.04f) ? rg.r(4.0f, 9.0f) : rg.r(0.5f, 2.2f);
            vec3 c = glm::mix(vec3(0.65f, 0.78f, 1.0f), vec3(1.0f, 0.88f, 0.70f), rg.f()) * bri;
            Vertex vt; vt.pos = vec3(r * s * std::cos(a), r * z, r * s * std::sin(a)); vt.col = c;
            vt.aux = vec4(rg.r(1.3f, 3.2f), rg.f(), 1.0f, 0.0f);
            v.push_back(vt);
        }
        starfield.upload(v, GL_POINTS);
    }

    // ---- cloud sea (soft billboards)
    Mesh clouds;
    {
        VList v;
        Rng rg(31337);
        const vec2 cs[6] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
        for (int c = 0; c < 190; ++c) {
            float a = rg.r(0, 2 * PI), r = 9.0f + std::pow(rg.f(), 0.75f) * 105.0f;
            vec3 center(r * std::cos(a), -9.5f - rg.r(0, 6.0f), r * std::sin(a));
            for (int p = 0; p < 6; ++p) {
                vec3 pc = center + vec3(rg.r(-7, 7), rg.r(-1.2f, 1.2f), rg.r(-7, 7));
                float sunLit = std::clamp(glm::dot(glm::normalize(vec2(pc.x, pc.z)), glm::normalize(vec2(-0.55f, -0.83f))), 0.0f, 1.0f);
                float depthT = std::clamp((pc.y + 15.5f) / 6.0f, 0.0f, 1.0f);
                vec3 col = glm::mix(vec3(0.26f, 0.13f, 0.48f), vec3(0.78f, 0.44f, 0.66f), depthT);
                col = glm::mix(col, vec3(1.0f, 0.75f, 0.55f), sunLit * 0.55f);
                float size = rg.r(10.0f, 24.0f), alpha = rg.r(0.035f, 0.085f);
                for (int k = 0; k < 6; ++k) {
                    Vertex vt; vt.pos = pc; vt.col = col; vt.aux = vec4(cs[k].x, cs[k].y, size, alpha);
                    v.push_back(vt);
                }
            }
        }
        clouds.upload(v, GL_TRIANGLES);
    }

    // ---- particles (dynamic GL_POINTS)
    const size_t MAXP = 7000;
    std::vector<Particle> parts;
    Mesh particleMesh;
    particleMesh.upload(VList(), GL_POINTS, MAXP);
    Rng prng(2026);
    auto emit = [&](vec3 p, vec3 v, vec3 c, float life, float size, float drag = 0.55f) {
        if (parts.size() < MAXP) parts.push_back(Particle{p, v, c, life, life, size, drag});
    };
    Mesh hudMesh;
    hudMesh.upload(VList(), GL_TRIANGLES, 90000);

    // selection ring (planet highlight)
    Mesh selRing;
    {
        VList r;
        std::vector<vec3> c;
        for (int i = 0; i < 120; ++i) c.push_back({1.12f * std::cos(2 * PI * i / 120), 0, 1.12f * std::sin(2 * PI * i / 120)});
        tube(r, c, 0.022f, 0.022f, 8, vec3(1.0f), true);
        VList orb = sphereMesh(0.06f, vec3(1.0f), 12, 6);
        for (int i = 0; i < 3; ++i) append(r, orb, glm::translate(mat4(1.0f), vec3(1.12f * std::cos(2 * PI * i / 3), 0, 1.12f * std::sin(2 * PI * i / 3))));
        selRing.upload(r, GL_TRIANGLES);
    }

    // ======================================================================
    //  INTERIOR WORLDS (milestone 0.5)
    // ======================================================================
    struct Info { const char* name; const char* prompt; };
    static const Info WI[4] = {{"01 Garden of Unborn Stars", "E: Bloom the flowers"},
                               {"02 Factory of Forgotten Sunsets", "E: Start the Sunset Engine"},
                               {"03 Library of Unwritten Universes", "E: Open the unwritten book"},
                               {"04 Kingdom Between Seconds", "E: Reverse the flow of time"}};
    const float OBSTACLE[4] = {1.5f, 3.5f, 1.5f, 4.1f};
    Mesh iRock[4], iTop[4], iInlay[4];
    auto annulusV = [&](VList& v, float r0, float r1, float y, vec3 c) { lathe(v, {{r0, y, c}, {r1, y, c}}, 140); };

    // --- shared island rocks
    iRock[0].upload(rockSurface(13, 8, 51, vec3(.20f, .34f, .34f), vec3(.08f, .12f, .22f), 96, 40), GL_TRIANGLES);
    iRock[1].upload(rockSurface(13, 8, 52, vec3(.42f, .20f, .14f), vec3(.14f, .07f, .12f), 96, 40), GL_TRIANGLES);
    iRock[2].upload(rockSurface(13, 8, 53, vec3(.30f, .20f, .46f), vec3(.12f, .08f, .26f), 96, 40), GL_TRIANGLES);
    iRock[3].upload(rockSurface(13, 8, 54, vec3(.40f, .50f, .70f), vec3(.12f, .16f, .34f), 96, 40), GL_TRIANGLES);

    // --- 01 GARDEN
    Mesh gTree, gOrbs, fStem, fHead;
    struct Flower { vec3 p; float s, rot; int c; };
    std::vector<Flower> flowers;
    {
        VList top, in;
        lathe(top, {{0, .18f, vec3(.16f, .42f, .34f)}, {9.5f, .14f, vec3(.22f, .54f, .40f)}, {12.6f, .05f, vec3(.20f, .42f, .38f)}, {13, 0, vec3(.20f, .34f, .34f)}}, 96);
        iTop[0].upload(top, GL_TRIANGLES);
        annulusV(in, 5.9f, 6.1f, .2f, MINT);
        annulusV(in, 9.0f, 9.12f, .16f, MINT);
        iInlay[0].upload(in, GL_TRIANGLES);
        VList tv, ov;
        vec3 bark(0.36f, 0.24f, 0.44f);
        auto trunkAt = [&](float t) { float an = t * 5.0f, rd = 0.5f * (1 - t * 0.7f); return vec3(rd * std::cos(an), t * 6.6f, rd * std::sin(an)); };
        std::vector<vec3> tr;
        for (int i = 0; i <= 80; ++i) tr.push_back(trunkAt(i / 80.0f));
        tube(tv, tr, 0.62f, 0.13f, 14, bark, false);
        lathe(tv, {{1.5f, 0, bark * 0.8f}, {0.85f, 0.5f, bark}, {0.62f, 1.1f, bark}}, 14);
        Rng rg(88);
        for (int b = 0; b < 10; ++b) {
            float t0 = rg.r(0.40f, 0.95f), an = rg.r(0, 2 * PI), L = 2.6f * (1.3f - t0), rise = rg.r(0.8f, 1.6f);
            vec3 P0 = trunkAt(t0), out(std::cos(an), 0, std::sin(an));
            std::vector<vec3> p;
            for (int i = 0; i < 14; ++i) { float s2 = i / 13.0f; p.push_back(P0 + out * (s2 * L) + vec3(0, s2 * 1.4f * rise - s2 * s2 * 0.4f, 0)); }
            tube(tv, p, 0.12f, 0.03f, 8, bark, false);
            append(ov, sphereMesh(0.22f, crystalPal[b % 4], 14, 8), glm::translate(mat4(1.0f), p.back()));
        }
        for (int r = 0; r < 6; ++r) {        // inverted roots reaching up into the sky
            float a = r * PI / 3;
            vec3 st = trunkAt(1.0f);
            std::vector<vec3> p;
            for (int i = 0; i < 18; ++i) { float s2 = i / 17.0f; p.push_back(st + vec3(std::cos(a + s2 * 2.0f) * s2 * 1.8f, s2 * 6.0f, std::sin(a + s2 * 2.0f) * s2 * 1.8f)); }
            tube(tv, p, 0.09f, 0.015f, 6, bark * 0.9f, false);
            append(ov, sphereMesh(0.12f, ICE, 12, 6), glm::translate(mat4(1.0f), p.back()));
        }
        for (int r = 0; r < 6; ++r) {        // ground roots
            float a = r * PI / 3 + 0.3f;
            std::vector<vec3> p;
            for (int i = 0; i < 14; ++i) { float s2 = i / 13.0f; p.push_back({std::cos(a + s2 * 0.5f) * (0.8f + s2 * 3.2f), 0.18f * (1 - s2) + 0.05f, std::sin(a + s2 * 0.5f) * (0.8f + s2 * 3.2f)}); }
            tube(tv, p, 0.20f, 0.04f, 8, bark * 0.85f, false);
        }
        for (int i = 0; i < 52; ++i) {
            float a = rg.r(0, 2 * PI), r = std::sqrt(rg.f()) * 3.4f;
            append(ov, sphereMesh(rg.r(0.06f, 0.14f), crystalPal[i % 4], 10, 6), glm::translate(mat4(1.0f), vec3(r * std::cos(a), 6.9f + rg.r(-1.0f, 1.6f), r * std::sin(a))));
        }
        gTree.upload(tv, GL_TRIANGLES);
        gOrbs.upload(ov, GL_TRIANGLES);
        VList st, hd;
        std::vector<vec3> sp;
        for (int i = 0; i <= 8; ++i) sp.push_back({0.10f * std::sin(i * 0.4f), 0.92f * i / 8.0f, 0});
        tube(st, sp, 0.035f, 0.02f, 6, vec3(.25f, .62f, .42f), false);
        lathe(hd, {{0, .90f, vec3(1.0f)}, {.08f, .95f, vec3(1.0f)}, {.24f, 1.12f, vec3(1.0f)}, {.32f, 1.36f, vec3(1.0f)}, {.20f, 1.32f, vec3(.85f)}, {.12f, 1.12f, vec3(.75f)}, {0, 1.02f, vec3(.7f)}}, 10);
        append(hd, sphereMesh(0.07f, vec3(1.0f, 0.9f, 0.5f), 10, 6), glm::translate(mat4(1.0f), vec3(0, 1.1f, 0)));
        fStem.upload(st, GL_TRIANGLES);
        fHead.upload(hd, GL_TRIANGLES);
        Rng fr(5);
        for (int i = 0; i < 38; ++i) {
            float a = fr.r(0, 2 * PI), r = fr.r(3.4f, 11.2f);
            if (std::fabs(r * std::cos(a)) < 1.5f && r * std::sin(a) > 3.5f) { --i; continue; }
            flowers.push_back({{r * std::cos(a), 0.12f, r * std::sin(a)}, fr.r(0.8f, 1.5f), fr.r(0, 6.28f), i % 4});
        }
    }

    // --- 02 FACTORY
    Mesh fTower, fGear[3], fArm1, fArm2;
    {
        VList top, in;
        lathe(top, {{0, .12f, vec3(.45f, .27f, .17f)}, {12.4f, .12f, vec3(.50f, .30f, .18f)}, {12.9f, .02f, vec3(.9f, .65f, .3f)}, {13, 0, vec3(.4f, .2f, .15f)}}, 96);
        iTop[1].upload(top, GL_TRIANGLES);
        annulusV(in, 4.6f, 4.75f, .13f, GOLD);
        annulusV(in, 8.0f, 8.15f, .13f, GOLD);
        annulusV(in, 11.5f, 11.65f, .13f, GOLD);
        for (int i = 0; i < 24; ++i) box(in, vec3(4.2f, 0.004f, 0.03f), GOLD, glm::rotate(mat4(1.0f), PI * i / 12, vec3(0, 1, 0)) * glm::translate(mat4(1.0f), vec3(7.8f, 0.135f, 0)));
        iInlay[1].upload(in, GL_TRIANGLES);
        VList tw;
        vec3 cu(.72f, .42f, .22f), go(.95f, .74f, .34f);
        lathe(tw, {{0, 0, cu}, {3.2f, 0, cu}, {3.2f, .4f, cu}, {2.6f, .5f, go}, {2.6f, 2.2f, cu}, {2.1f, 2.4f, go}, {1.5f, 3.0f, cu}, {1.3f, 3.6f, cu},
                   {1.2f, 5.6f, cu}, {1.7f, 5.8f, go}, {2.1f, 6.2f, cu}, {2.35f, 6.6f, go}, {1.95f, 6.7f, cu}, {1.5f, 6.0f, cu * 0.5f}}, 48);
        for (int i = 0; i < 6; ++i) {
            float a = i * PI / 3 + 0.4f, h = 3.0f + (i % 3) * 1.1f;
            VList ch;
            lathe(ch, {{0, 0, cu}, {0.55f, 0, cu}, {0.50f, h, cu}, {0.68f, h + .1f, go}, {0.40f, h + .2f, cu * 0.4f}}, 16);
            append(tw, ch, glm::translate(mat4(1.0f), vec3(10.4f * std::cos(a), 0.1f, 10.4f * std::sin(a))));
        }
        fTower.upload(tw, GL_TRIANGLES);
        fGear[0].upload(gearMesh(12, vec3(.80f, .55f, .26f)), GL_TRIANGLES);
        fGear[1].upload(gearMesh(16, vec3(.92f, .70f, .32f)), GL_TRIANGLES);
        fGear[2].upload(gearMesh(20, vec3(.70f, .42f, .24f)), GL_TRIANGLES);
        auto arm = [&](float L, float r0, float r1) {
            VList a;
            std::vector<vec3> p;
            for (int i = 0; i <= 6; ++i) p.push_back({L * i / 6.0f, 0, 0});
            tube(a, p, r0, r1, 10, vec3(.85f, .62f, .30f), false);
            append(a, sphereMesh(r0 * 1.5f, vec3(.95f, .74f, .34f), 14, 8));
            return a;
        };
        fArm1.upload(arm(3.2f, 0.16f, 0.12f), GL_TRIANGLES);
        fArm2.upload(arm(2.6f, 0.12f, 0.06f), GL_TRIANGLES);
    }

    // --- 03 LIBRARY
    Mesh lShelves, lBook, lBird, lConst, lConstNodes;
    {
        VList top, in;
        lathe(top, {{0, .10f, vec3(.82f, .76f, .94f)}, {12.5f, .10f, vec3(.80f, .74f, .92f)}, {13, 0, vec3(.55f, .48f, .72f)}}, 96);
        iTop[2].upload(top, GL_TRIANGLES);
        annulusV(in, 3.0f, 3.12f, .12f, GOLD);
        annulusV(in, 6.5f, 6.62f, .12f, GOLD);
        annulusV(in, 12.0f, 12.2f, .12f, GOLD);
        for (int i = 0; i < 12; ++i) box(in, vec3(4.5f, 0.004f, 0.03f), GOLD, glm::rotate(mat4(1.0f), PI * i / 6, vec3(0, 1, 0)) * glm::translate(mat4(1.0f), vec3(7.6f, 0.125f, 0)));
        iInlay[2].upload(in, GL_TRIANGLES);
        VList sh;
        Rng rg(606);
        vec3 wood(.30f, .18f, .16f);
        const vec3 bookC[5] = {{.55f, .15f, .30f}, {.20f, .30f, .55f}, {.85f, .70f, .30f}, {.25f, .50f, .45f}, {.60f, .40f, .75f}};
        for (int k = 0; k < 46; ++k) {
            float t = k / 45.0f, an = t * 3.4f * PI, y = t * 10.0f;
            mat4 U = glm::translate(mat4(1.0f), vec3(10.2f * std::cos(an), y, 10.2f * std::sin(an))) * glm::rotate(mat4(1.0f), PI / 2 - an, vec3(0, 1, 0));
            box(sh, vec3(1.3f, 0.55f, 0.05f), wood, U * glm::translate(mat4(1.0f), vec3(0, 0.55f, 0.35f)));
            box(sh, vec3(1.3f, 0.04f, 0.35f), wood * 1.2f, U);
            box(sh, vec3(1.3f, 0.04f, 0.35f), wood * 1.2f, U * glm::translate(mat4(1.0f), vec3(0, 1.1f, 0)));
            box(sh, vec3(0.04f, 0.55f, 0.35f), wood, U * glm::translate(mat4(1.0f), vec3(-1.3f, 0.55f, 0)));
            box(sh, vec3(0.04f, 0.55f, 0.35f), wood, U * glm::translate(mat4(1.0f), vec3(1.3f, 0.55f, 0)));
            float x = -1.18f;
            while (x < 1.1f) {
                float bw = rg.r(0.07f, 0.13f), bh = rg.r(0.34f, 0.50f);
                box(sh, vec3(bw, bh * 0.5f, 0.17f), bookC[(int)(rg.f() * 5) % 5] * rg.r(0.8f, 1.2f), U * glm::translate(mat4(1.0f), vec3(x + bw, 0.04f + bh * 0.5f, 0.12f)));
                x += bw * 2 + 0.012f;
            }
        }
        lShelves.upload(sh, GL_TRIANGLES);
        VList pg;
        surface(pg, [](float u, float v) { return vec3(u * 1.5f, 0.06f + 0.05f * std::sin(u * PI), (v - 0.5f) * 2.1f); },
                [](float, float v) { float l = std::sin(v * 70.0f) > 0.85f ? 0.5f : 0.0f; return glm::mix(vec3(.96f, .92f, .82f), vec3(.65f, .56f, .46f), l); }, 12, 40);
        box(pg, vec3(0.76f, 0.03f, 1.1f), vec3(.38f, .16f, .55f), glm::translate(mat4(1.0f), vec3(0.75f, -0.01f, 0)));
        lBook.upload(pg, GL_TRIANGLES);
        VList bd;
        const vec3 paper(.96f, .93f, .86f);
        tri(bd, Vertex{{0, .02f, .30f}, {0, 1, 0}, paper}, Vertex{{-.55f, 0, -.25f}, {0, 1, 0}, paper}, Vertex{{0, 0, -.30f}, {0, 1, 0}, paper});
        tri(bd, Vertex{{0, .02f, .30f}, {0, 1, 0}, paper}, Vertex{{.55f, 0, -.25f}, {0, 1, 0}, paper}, Vertex{{0, 0, -.30f}, {0, 1, 0}, paper});
        tri(bd, Vertex{{0, 0, -.30f}, {0, 1, 0}, paper * 0.8f}, Vertex{{-.12f, 0, -.65f}, {0, 1, 0}, paper * 0.8f}, Vertex{{.12f, 0, -.65f}, {0, 1, 0}, paper * 0.8f});
        lBird.upload(bd, GL_TRIANGLES);
        VList cl, cn;
        for (int g = 0; g < 5; ++g) {
            float an = g * 2 * PI / 5;
            vec3 c0(5.2f * std::cos(an), 5.5f + 1.3f * std::sin(g * 2.1f), 5.2f * std::sin(an)), tg(-std::sin(an), 0, std::cos(an));
            vec3 prev(0);
            for (int k = 0; k < 7; ++k) {
                vec3 p = c0 + tg * rg.r(-1.3f, 1.3f) + vec3(0, rg.r(-1.0f, 1.0f), 0);
                append(cn, sphereMesh(0.08f, vec3(1.0f, 0.92f, 0.7f), 10, 6), glm::translate(mat4(1.0f), p));
                if (k > 0) { cl.push_back({prev, {0, 1, 0}, vec3(0.8f, 0.7f, 1.0f)}); cl.push_back({p, {0, 1, 0}, vec3(0.8f, 0.7f, 1.0f)}); }
                prev = p;
            }
        }
        lConst.upload(cl, GL_LINES);
        lConstNodes.upload(cn, GL_TRIANGLES);
    }

    // --- 04 KINGDOM
    Mesh kGlass, kSandLow, kSandUp, kFrame, kRail, kPlank, kClock;
    {
        VList top, in;
        lathe(top, {{0, .10f, vec3(.86f, .92f, 1.0f)}, {12.5f, .10f, vec3(.80f, .88f, 1.0f)}, {13, 0, vec3(.45f, .55f, .78f)}}, 96);
        Rng rg(404);
        for (int i = 0; i < 30; ++i) {
            float a = rg.r(0, 2 * PI), r = rg.r(5.4f, 11.8f), rad = rg.r(0.4f, 0.8f), h = rg.r(1.6f, 5.2f);
            if (std::fabs(r * std::cos(a)) < 1.8f && r * std::sin(a) > 3.0f) continue;   // keep the spawn corridor clear
            vec3 st(.86f, .92f, 1.0f), roof = (i % 3 == 0) ? vec3(.95f, .76f, .36f) : vec3(.40f, .60f, .90f);
            VList tw;
            lathe(tw, {{rad, 0, st}, {rad, h, st}, {rad * 1.25f, h, roof}, {0, h + rad * 2.4f, roof}}, 10);
            append(top, tw, glm::translate(mat4(1.0f), vec3(r * std::cos(a), 0.1f, r * std::sin(a))));
        }
        iTop[3].upload(top, GL_TRIANGLES);
        annulusV(in, 4.4f, 4.5f, .12f, GOLD);
        annulusV(in, 12.0f, 12.2f, .12f, GOLD);
        iInlay[3].upload(in, GL_TRIANGLES);
        auto rr = [](float y) { return 0.55f + 2.6f * std::pow(std::fabs(y - 11.9f) / 11.1f, 1.6f); };
        std::vector<Prof> gp, sl, su;
        for (int i = 0; i <= 44; ++i) { float y = 0.8f + 22.2f * i / 44.0f; gp.push_back({rr(y), y, vec3(1.0f)}); }
        VList gv, slv, suv;
        lathe(gv, gp, 48);
        kGlass.upload(gv, GL_TRIANGLES);
        lathe(slv, {{0, 1.0f, vec3(1.0f, .85f, .5f)}, {2.2f, 1.0f, vec3(1.0f, .85f, .5f)}, {0.9f, 3.0f, vec3(1.0f, .9f, .6f)}, {0, 3.6f, vec3(1.0f)}}, 32);
        lathe(suv, {{0, 22.8f, vec3(1.0f, .85f, .5f)}, {2.2f, 22.8f, vec3(1.0f, .85f, .5f)}, {0.9f, 20.8f, vec3(1.0f, .9f, .6f)}, {0, 20.2f, vec3(1.0f)}}, 32);
        kSandLow.upload(slv, GL_TRIANGLES);
        kSandUp.upload(suv, GL_TRIANGLES);
        VList fr;
        for (int i = 0; i < 3; ++i) {
            float a = i * 2 * PI / 3 + 0.5f;
            std::vector<vec3> p{{3.9f * std::cos(a), 0.4f, 3.9f * std::sin(a)}, {3.9f * std::cos(a), 23.4f, 3.9f * std::sin(a)}};
            tube(fr, p, 0.14f, 0.14f, 10, GOLD * 0.9f, false);
        }
        lathe(fr, {{0, 0.4f, GOLD * 0.8f}, {4.4f, 0.4f, GOLD * 0.9f}, {4.4f, 0.75f, GOLD}, {0, 0.75f, GOLD}}, 48);
        lathe(fr, {{0, 23.3f, GOLD * 0.8f}, {4.4f, 23.3f, GOLD * 0.9f}, {4.4f, 23.65f, GOLD}, {0, 23.65f, GOLD}}, 48);
        kFrame.upload(fr, GL_TRIANGLES);
        VList rl, pk, ck;
        const int NP = 8;
        for (int i = 0; i < NP; ++i) {
            float a = i * 2 * PI / NP;
            VList py;
            lathe(py, {{0.35f, 0, GOLD * 0.9f}, {0.22f, 7.0f, GOLD}, {0.40f, 7.2f, GOLD}, {0, 7.8f, GOLD}}, 12);
            append(rl, py, glm::translate(mat4(1.0f), vec3(8.5f * std::cos(a), 0.1f, 8.5f * std::sin(a))));
        }
        for (int side = -1; side <= 1; side += 2) {
            std::vector<vec3> p;
            for (int i = 0; i <= NP * 12; ++i) { float a = 2 * PI * i / (NP * 12.0f), s2 = std::fmod(i / 12.0f, 1.0f); float y = 7.0f + 0.7f * std::sin(PI * s2); p.push_back({(8.5f + side * 0.75f) * std::cos(a), y + 0.65f, (8.5f + side * 0.75f) * std::sin(a)}); }
            p.pop_back();
            tube(rl, p, 0.05f, 0.05f, 6, GOLD, true);
        }
        for (int i = 0; i < NP * 12; ++i) {
            float a = 2 * PI * (i + 0.5f) / (NP * 12.0f), s2 = std::fmod((i + 0.5f) / 12.0f, 1.0f);
            float y = 7.0f + 0.7f * std::sin(PI * s2);
            box(pk, vec3(0.8f, 0.04f, 0.19f), vec3(.65f, .85f, 1.0f), glm::translate(mat4(1.0f), vec3(8.5f * std::cos(a), y, 8.5f * std::sin(a))) * glm::rotate(mat4(1.0f), -a, vec3(0, 1, 0)));
        }
        kRail.upload(rl, GL_TRIANGLES);
        kPlank.upload(pk, GL_TRIANGLES);
        std::vector<vec3> cc;
        for (int i = 0; i < 100; ++i) cc.push_back({1.7f * std::cos(2 * PI * i / 100), 0, 1.7f * std::sin(2 * PI * i / 100)});
        tube(ck, cc, 0.04f, 0.04f, 8, GOLD, true);
        for (int i = 0; i < 12; ++i) box(ck, vec3(0.04f, 0.04f, 0.18f), GOLD, glm::rotate(mat4(1.0f), -2 * PI * i / 12, vec3(0, 1, 0)) * glm::translate(mat4(1.0f), vec3(1.7f, 0, 0)));
        kClock.upload(ck, GL_TRIANGLES);
    }

    // ---- main-pass draw helper (shared by hub and interiors)
    auto draw = [&](const Mesh& m, const mat4& model, const Mat& mt) {
        scene.m4("uModel", model);
        scene.i1("uMode", mt.mode);
        scene.f1("uSpec", mt.spec);
        scene.f1("uShin", mt.shin);
        scene.f1("uEmit", mt.emit);
        scene.f1("uRefl", mt.refl);
        scene.f1("uFlap", mt.flap);
        scene.f1("uFlapAmp", mt.fAmp);
        scene.f1("uFlapLen", mt.fLen);
        scene.f1("uFlapSpd", mt.fSpd);
        scene.v3("uTint", mt.tint);
        m.draw();
    };
    const mat4 I(1.0f);
    auto armPose = [&](int i, float t, float mach, mat4& m1, mat4& m2, vec3& tip) {
        float yaw = i * 2 * PI / 3 + t * 0.25f * mach, e1 = 0.35f + 0.25f * std::sin(t * 0.8f + i), e2 = -0.55f + 0.5f * std::sin(t * 1.1f + i * 2.0f);
        m1 = glm::translate(I, vec3(0, 5.0f, 0)) * glm::rotate(I, -yaw, vec3(0, 1, 0)) * glm::rotate(I, e1, vec3(0, 0, 1));
        m2 = m1 * glm::translate(I, vec3(3.2f, 0, 0)) * glm::rotate(I, e2, vec3(0, 0, 1));
        tip = vec3(m2 * vec4(2.6f, 0, 0, 1));
    };

    auto drawInterior = [&](int wd, float t, float act, float flow) {
        draw(iRock[wd], I, M_STONE);
        switch (wd) {
        case 0: {
            draw(iTop[0], I, M_STONE);
            draw(iInlay[0], I, emissive(1.4f + act));
            draw(gTree, I, Mat{0, 0.25f, 24, 0.0f, 0.05f});
            draw(gOrbs, I, emissive(1.6f + act * 2.4f));
            for (const Flower& f : flowers) {
                mat4 M = glm::translate(I, f.p) * glm::rotate(I, f.rot, vec3(0, 1, 0)) * glm::scale(I, vec3(f.s));
                draw(fStem, M, M_STONE);
                float open = 0.35f + 0.65f * act;
                Mat hm = M_CRYSTAL; hm.emit = 0.5f + act * 1.4f; hm.tint = crystalPal[f.c];
                draw(fHead, M * glm::scale(I, vec3(open, 1.0f, open)), hm);
            }
        } break;
        case 1: {
            float mach = 0.15f + 0.85f * act;
            draw(iTop[1], I, M_BRASS);
            draw(iInlay[1], I, M_INLAY);
            draw(fTower, I, M_BRASS);
            for (int i = 0; i < 6; ++i) {
                float a = i * PI / 3, R = 1.3f + 0.45f * (i % 3), dir = (i % 2) ? 1.0f : -1.0f;
                vec3 p(4.9f * std::cos(a), 1.6f + (i % 2) * 1.0f, 4.9f * std::sin(a));
                draw(fGear[i % 3], glm::translate(I, p) * glm::rotate(I, PI / 2 - a, vec3(0, 1, 0)) * glm::rotate(I, dir * t * mach * 0.9f / R, vec3(0, 0, 1)) * glm::scale(I, vec3(R)), M_BRASS);
            }
            for (int i = 0; i < 3; ++i) {
                mat4 m1, m2; vec3 tip;
                armPose(i, t, mach, m1, m2, tip);
                draw(fArm1, m1, M_BRASS);
                draw(fArm2, m2, M_BRASS);
                draw(moon, glm::translate(I, tip) * glm::scale(I, vec3(1.6f)), emissive(2.6f, vec3(1.0f, 0.55f, 0.2f)));
            }
            vec3 sp(0, 6.9f + act * 5.2f + 0.15f * std::sin(t), 0);
            draw(core, glm::translate(I, sp) * glm::scale(I, vec3(0.45f + 0.65f * act)), emissive(3.2f, vec3(1.0f, 0.62f, 0.25f)));
        } break;
        case 2: {
            draw(iTop[2], I, M_MARBLE);
            draw(iInlay[2], I, M_INLAY);
            draw(lShelves, I, M_STONE);
            mat4 B = glm::translate(I, vec3(0, 2.2f + 0.15f * std::sin(t * 0.9f), 0)) * glm::rotate(I, t * 0.25f, vec3(0, 1, 0));
            float ang = 1.5f - 1.28f * act;
            Mat pm = M_STONE; pm.emit = 0.3f + act * 0.5f;
            draw(lBook, B * glm::rotate(I, ang, vec3(0, 0, 1)), pm);
            draw(lBook, B * glm::rotate(I, -ang, vec3(0, 0, 1)) * glm::scale(I, vec3(-1, 1, 1)), pm);
            if (act > 0.02f) {
                draw(planets[2], glm::translate(I, vec3(0, 3.7f + 0.12f * std::sin(t * 1.3f), 0)) * glm::rotate(I, t * 0.3f, vec3(0, 1, 0)) * glm::scale(I, vec3(act)), M_PLANET);
                draw(lConst, I, emissive(act * 2.4f));
                draw(lConstNodes, I, emissive(act * 3.0f));
            }
            Mat bm = M_STONE; bm.flap = 1.0f; bm.fAmp = 0.32f; bm.fLen = 0.5f; bm.fSpd = 9.0f; bm.emit = 0.25f;
            for (int i = 0; i < 8; ++i) {
                float th = t * (0.30f + 0.03f * i) * (i % 2 ? 1 : -1) + i, r = 5.0f + 0.6f * i, y = 3.0f + 1.2f * std::sin(i * 1.3f) + 0.3f * std::sin(t + i);
                float sgn = (i % 2) ? 1.0f : -1.0f;
                draw(lBird, glm::translate(I, vec3(r * std::cos(th), y, r * std::sin(th))) * glm::rotate(I, -th * sgn + (sgn < 0 ? PI : 0.0f), vec3(0, 1, 0)) * glm::rotate(I, -0.25f, vec3(0, 0, 1)) * glm::scale(I, vec3(1.3f)), bm);
            }
        } break;
        case 3: {
            for (int up = 0; up < 2; ++up) {
                mat4 M = up ? glm::translate(I, vec3(0, 24.0f, 0)) * glm::rotate(I, PI, vec3(1, 0, 0)) : I;
                if (up) draw(iRock[3], M, M_STONE);
                draw(iTop[3], M, M_MARBLE);
                draw(iInlay[3], M, M_INLAY);
            }
            draw(kFrame, I, M_BRASS);
            draw(kRail, glm::rotate(I, PI / 8, vec3(0, 1, 0)), M_BRASS);
            Mat pk = M_CRYSTAL; pk.emit = 0.1f + act * 1.5f; pk.tint = vec3(0.65f, 0.85f, 1.0f);
            draw(kPlank, glm::rotate(I, PI / 8, vec3(0, 1, 0)), pk);
            draw(kClock, glm::translate(I, vec3(0, 11.9f, 0)) * glm::rotate(I, t * 0.4f * flow, vec3(0, 1, 0)), M_BRASS);
            draw(kClock, glm::translate(I, vec3(0, 11.9f, 0)) * glm::rotate(I, 1.2f, vec3(1, 0, 0)) * glm::rotate(I, -t * 0.6f * flow, vec3(0, 1, 0)) * glm::scale(I, vec3(0.8f)), M_BRASS);
            float lowF = 0.75f + 0.25f * flow, upF = 0.75f - 0.25f * flow;
            draw(kSandLow, glm::scale(I, vec3(1.0f, lowF, 1.0f)), emissive(1.5f, vec3(1.0f, 0.85f, 0.55f)));
            draw(kSandUp, glm::translate(I, vec3(0, 22.8f, 0)) * glm::scale(I, vec3(1.0f, upF, 1.0f)) * glm::translate(I, vec3(0, -22.8f, 0)), emissive(1.5f, vec3(1.0f, 0.85f, 0.55f)));
            draw(core, glm::translate(I, vec3(0, 11.9f, 0)) * glm::scale(I, vec3(0.65f)), emissive(3.0f, vec3(0.7f, 0.9f, 1.0f)));
        } break;
        }
    };
    auto drawInteriorGlow = [&](int wd, float t, float act, float flow) {
        (void)flow;
        switch (wd) {
        case 0: draw(corona, glm::translate(I, vec3(0, 6.9f, 0)) * glm::scale(I, vec3(1.7f)), glass(0.5f + act * 1.4f, MINT)); break;
        case 1: {
            vec3 sp(0, 6.9f + act * 5.2f + 0.15f * std::sin(t), 0);
            draw(corona, glm::translate(I, sp) * glm::scale(I, vec3((0.45f + 0.65f * act) * 1.3f)), glass(3.2f, vec3(1.0f, 0.5f, 0.15f)));
        } break;
        case 2:
            if (act > 0.02f) draw(shell, glm::translate(I, vec3(0, 3.7f, 0)) * glm::scale(I, vec3(act * 1.05f)), glass(2.6f, vec3(0.8f, 0.5f, 1.0f)));
            draw(corona, glm::translate(I, vec3(0, 2.4f, 0)) * glm::scale(I, vec3(0.9f + act)), glass(1.4f + act * 2.0f, vec3(0.8f, 0.55f, 1.0f)));
            break;
        case 3:
            draw(kGlass, I, glass(1.7f, vec3(0.55f, 0.8f, 1.0f)));
            draw(corona, glm::translate(I, vec3(0, 11.9f, 0)) * glm::scale(I, vec3(1.3f)), glass(2.8f, vec3(0.6f, 0.85f, 1.0f)));
            break;
        }
    };

    // ======================================================================
    //  RUNTIME STATE
    // ======================================================================
    Targets T;
    int lastW = 0, lastH = 0;
    const float ang4[4] = {0, PI / 2, PI, 3 * PI / 2};
    vec3 wPos[4];
    for (int i = 0; i < 4; ++i) wPos[i] = vec3(4.2f * std::cos(ang4[i]), 1.5f, 4.2f * std::sin(ang4[i]));
    float wScale[4] = {1, 1, 1, 1}, wAct[4] = {0, 0, 0, 0}, wActS[4] = {0, 0, 0, 0};
    int selected = startSelect, hover = -1, sceneId = -1, target = -1, mode = M_HUB;
    bool hoverAtlas = false, focus = false;
    float focusBlend = 0.0f, stateT = 0.0f, walkPh = 0.0f;
    auto spawnOf = [](int wd) { return vec3(0, 1.7f, wd == 1 ? 8.3f : (wd == 3 ? 9.4f : 6.8f)); };
    vec3 startEye(0), startTarget(0), endEye(0), pEye = spawnOf(startWorld >= 0 ? startWorld : 0);
    float pYaw = 0.0f, pPitch = 0.0f;
    double lastClickTime = -10.0;
    int lastClickWorld = -2;
    const float T_APPR = 1.2f, T_CROSS = 2.2f, T_ARR = 4.0f, T_RSW = 1.0f, T_REND = 2.6f;
    const vec3 coreCenter(0, 2.4f, 0);

    if (startWorld >= 0 && startWorld < 4) { sceneId = target = startWorld; mode = M_EXPLORE; state.skipIntro = true; }
    if (startAct && startWorld >= 0) wAct[startWorld] = wActS[startWorld] = 1.0f;
    state.mode = mode;

    std::cout << "CONTROLS: click planet = select | click Atlas = rotate | Tab = cycle | Enter/double-click = enter world | "
                 "scroll = scale selected | F focus | WASD+right-drag explore | E interact | Esc/Backspace return | "
                 "Space auto-orbit | B bloom | P screenshot | F11 fullscreen | R reset\n";

    double lastTime = glfwGetTime(), fpsTime = lastTime;
    int fpsFrames = 0, frameNo = 0;
    float introT = state.skipIntro ? INTRO_LEN : 0.0f;
    auto sm = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * x * (x * (x * 6 - 15) + 10); };
    auto setSky = [&](Prog& p, const Palette& pl) {
        p.v3("uSkyH", pl.h); p.v3("uSkyL", pl.l); p.v3("uSkyM", pl.m); p.v3("uSkyT", pl.t);
        p.v3("uSunDir", pl.sun); p.v3("uNeb0", pl.neb0); p.v3("uNeb1", pl.neb1);
    };
    auto bezier = [](vec3 a, vec3 b, vec3 c, vec3 d, float u) {
        float v = 1 - u;
        return v * v * v * a + 3 * v * v * u * b + 3 * v * u * u * c + u * u * u * d;
    };
    float accA = 0, accB = 0, accC = 0;

    // ========================================================== MAIN LOOP
    while (!glfwWindowShouldClose(w)) {
        glfwPollEvents();
        int width = 0, height = 0, winW = 1, winH = 1;
        glfwGetFramebufferSize(w, &width, &height);
        if (width == 0 || height == 0) { glfwWaitEventsTimeout(0.1); continue; }
        glfwGetWindowSize(w, &winW, &winH);
        winW = std::max(winW, 1); winH = std::max(winH, 1);
        if (width != lastW || height != lastH) { T.create(width, height); lastW = width; lastH = height; }

        double now = glfwGetTime();
        float dt = std::min(float(now - lastTime), 0.1f);
        lastTime = now;
        float t = (float)now;

        const bool introActive = introT < INTRO_LEN;
        if (state.skipIntro) introT = std::max(introT, INTRO_LEN); else introT += dt;
        if (introActive && state.clickL) state.clickL = false;          // first click only skips the intro

        state.atlasAngle += (state.atlasTarget - state.atlasAngle) * (1.0f - std::exp(-dt * 5.0f));
        for (int i = 0; i < 4; ++i) wActS[i] += (wAct[i] - wActS[i]) * (1.0f - std::exp(-dt * 1.8f));

        // ----- hub camera (exhibition mode)
        float ik = 1.0f - std::min(introT / INTRO_LEN, 1.0f);
        ik = ik * ik * ik;
        focusBlend += ((focus && selected >= 0 ? 1.0f : 0.0f) - focusBlend) * (1.0f - std::exp(-dt * 4.0f));
        vec3 hubTarget = glm::mix(vec3(0, 1.4f, 0), selected >= 0 ? wPos[selected] : vec3(0, 1.4f, 0), focusBlend);
        float hYaw = state.yaw + ik * 1.4f, hPitch = state.pitch + ik * 0.15f, hDist = state.distance + ik * 17.0f - focusBlend * 9.0f;
        vec3 hubEye = vec3(hDist * std::cos(hPitch) * std::sin(hYaw), 4.0f + hDist * std::sin(hPitch), hDist * std::cos(hPitch) * std::cos(hYaw)) + (hubTarget - vec3(0, 1.4f, 0));
        mat4 proj = glm::perspective(glm::radians(55.0f), float(width) / float(height), 0.2f, 220.0f);
        mat4 hubView = glm::lookAt(hubEye, hubTarget, vec3(0, 1, 0));

        // ----- 3D ray picking (planets first, then the Atlas)
        hover = -1;
        hoverAtlas = false;
        if (mode == M_HUB && introT >= INTRO_LEN && !state.dragging && glfwGetWindowAttrib(w, GLFW_HOVERED)) {
            mat4 inv = glm::inverse(proj * hubView);
            float nx = float(state.curX / winW) * 2.0f - 1.0f, ny = 1.0f - float(state.curY / winH) * 2.0f;
            vec4 a = inv * vec4(nx, ny, -1, 1), b = inv * vec4(nx, ny, 1, 1);
            vec3 O = vec3(a) / a.w, D = glm::normalize(vec3(b) / b.w - O);
            float best = 1e9f;
            for (int i = 0; i < 4; ++i) { float tt; if (raySphere(O, D, wPos[i], 0.82f * wScale[i], tt) && tt < best) { best = tt; hover = i; } }
            if (hover < 0) { float tt; hoverAtlas = raySphere(O, D, coreCenter, 2.7f, tt); }
        }

        // ----- input dispatch
        auto enterWorld = [&](int wd) {
            if (mode != M_HUB) return;
            target = wd; selected = wd; mode = M_APPROACH; stateT = 0.0f;
            startEye = hubEye; startTarget = hubTarget;
            std::cout << "Entering: " << WI[wd].name << '\n';
        };
        if (mode == M_HUB) {
            if (state.clickL) {
                if (hover >= 0) {
                    if (selected == hover && now - lastClickTime < 0.45 && lastClickWorld == hover) enterWorld(hover);
                    else { selected = hover; std::cout << "Selected: " << WI[hover].name << '\n'; }
                    lastClickTime = now; lastClickWorld = hover;
                } else if (hoverAtlas) {
                    state.atlasTarget += PI / 4; state.burst = true;
                    std::cout << "Atlas rotated 45 degrees\n";
                } else { selected = -1; focus = false; }
            }
            if (state.tab) selected = selected < 0 ? (state.tab > 0 ? 0 : 3) : (selected + state.tab + 4) % 4;
            if (state.enter && selected >= 0) enterWorld(selected);
            if (state.focus && selected >= 0) focus = !focus;
            if (state.space) state.autoOrbit = !state.autoOrbit;
            if (state.esc) glfwSetWindowShouldClose(w, GLFW_TRUE);
            if (state.reset) { state.atlasTarget = 0; for (float& s2 : wScale) s2 = 1; state.yaw = 0.55f; state.pitch = 0.30f; selected = -1; focus = false; }
            if (state.scroll != 0.0f) {
                int idx = selected >= 0 ? selected : 0;          // no selection: the green Garden world
                wScale[idx] = std::clamp(wScale[idx] * std::pow(1.1f, state.scroll), 0.45f, 2.8f);
                std::cout << "World scale: " << wScale[idx] << '\n';
            }
            float kd = dt * 1.4f;
            if (glfwGetKey(w, GLFW_KEY_LEFT) == GLFW_PRESS) state.yaw -= kd;
            if (glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS) state.yaw += kd;
            if (glfwGetKey(w, GLFW_KEY_UP) == GLFW_PRESS) state.pitch = std::clamp(state.pitch + kd, -1.2f, 1.2f);
            if (glfwGetKey(w, GLFW_KEY_DOWN) == GLFW_PRESS) state.pitch = std::clamp(state.pitch - kd, -1.2f, 1.2f);
            if (state.autoOrbit && !state.dragging) state.yaw += dt * 0.09f;
        } else if (mode == M_EXPLORE) {
            if (state.esc || state.back) { mode = M_RETURN; stateT = 0.0f; }
            if (state.interact && sceneId >= 0) { wAct[sceneId] = 1.0f - wAct[sceneId]; std::cout << WI[sceneId].name << (wAct[sceneId] > 0.5f ? ": activated\n" : ": deactivated\n"); }
            if (state.reset) { pEye = spawnOf(sceneId); pYaw = pPitch = 0.0f; }
        } else {                                                    // transitions: skip option
            if (state.space || state.enter || state.clickL || state.esc) {
                if (mode == M_APPROACH || mode == M_CROSS) stateT = T_CROSS;
                else if (mode == M_ARRIVE) stateT = T_ARR;
                else if (mode == M_RETURN) stateT = stateT < T_RSW ? T_RSW : T_REND;
            }
        }
        if (autoEnter >= 0 && frameNo == 3 && mode == M_HUB) { enterWorld(autoEnter); autoEnter = -1; }
        state.clickL = state.enter = state.esc = state.space = state.back = state.interact = state.focus = state.reset = false;
        state.tab = 0; state.scroll = 0.0f;

        // ----- exploration movement (WASD + mouse look, bounded)
        if (mode == M_EXPLORE) {
            pYaw += state.lookDX * 0.004f;
            pPitch = std::clamp(pPitch - state.lookDY * 0.004f, -1.3f, 1.3f);
            vec3 f(std::sin(pYaw), 0, -std::cos(pYaw)), r(std::cos(pYaw), 0, std::sin(pYaw)), mv(0);
            if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) mv += f;
            if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) mv -= f;
            if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) mv += r;
            if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) mv -= r;
            if (glm::length(mv) > 0.0f) {
                float sp = (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 9.0f : 5.0f;
                pEye += glm::normalize(mv) * sp * dt;
                walkPh += dt * 8.0f;
            }
            float rr = std::sqrt(pEye.x * pEye.x + pEye.z * pEye.z);
            const float LIM[4] = {11.6f, 11.6f, 9.0f, 12.0f};
            float obs = OBSTACLE[sceneId] + 0.4f, lim = LIM[sceneId];
            if (rr > lim) { pEye.x *= lim / rr; pEye.z *= lim / rr; }
            else if (rr < obs && rr > 1e-4f) { pEye.x *= obs / rr; pEye.z *= obs / rr; }
        }
        state.lookDX = state.lookDY = 0.0f;

        // ----- transition state machine
        if (mode == M_APPROACH || mode == M_CROSS) {
            stateT += dt;
            mode = stateT < T_APPR ? M_APPROACH : M_CROSS;
            if (stateT >= T_CROSS) { sceneId = target; mode = M_ARRIVE; stateT = T_CROSS; pEye = spawnOf(target); pYaw = pPitch = 0.0f; }
        } else if (mode == M_ARRIVE) {
            stateT += dt;
            if (stateT >= T_ARR) mode = M_EXPLORE;
        } else if (mode == M_RETURN) {
            stateT += dt;
            if (stateT >= T_RSW && sceneId >= 0) sceneId = -1;
            if (stateT >= T_REND) mode = M_HUB;
        }
        state.mode = mode;

        // ----- veil strength + camera
        float veil = 0.0f;
        if (mode == M_CROSS) veil = sm((stateT - T_APPR) / (T_CROSS - T_APPR));
        else if (mode == M_ARRIVE) veil = 1.0f - sm((stateT - T_CROSS) / (T_ARR - T_CROSS));
        else if (mode == M_RETURN) veil = stateT < T_RSW ? sm(stateT / T_RSW) : 1.0f - sm((stateT - T_RSW) / (T_REND - T_RSW));
        vec3 veilCol = target >= 0 ? worldTint[target] : vec3(1.0f);

        vec3 eye = hubEye, tgt = hubTarget;
        vec3 pc = target >= 0 ? wPos[target] : vec3(0);
        float pr = target >= 0 ? 0.86f * wScale[target] : 1.0f;
        if (target >= 0) {
            vec3 dirOut = glm::normalize(startEye - pc);
            endEye = pc + dirOut * (pr * 2.7f);
        }
        vec3 fwd(std::cos(pPitch) * std::sin(pYaw), std::sin(pPitch), -std::cos(pPitch) * std::cos(pYaw));
        if (mode == M_APPROACH) {
            float p = sm(stateT / T_APPR);
            eye = bezier(startEye, startEye + (endEye - startEye) * 0.25f + vec3(0, 2.5f, 0), endEye + (startEye - endEye) * 0.15f + vec3(0, 1.0f, 0), endEye, p);
            tgt = glm::mix(startTarget, pc, p);
        } else if (mode == M_CROSS) {
            float u = sm((stateT - T_APPR) / (T_CROSS - T_APPR));
            eye = glm::mix(endEye, pc + glm::normalize(endEye - pc) * pr * 0.2f, u * u);
            tgt = pc;
        } else if (mode == M_ARRIVE) {
            float p = sm((stateT - T_CROSS) / (T_ARR - T_CROSS));
            eye = pEye + vec3(0, 0.7f, 5.0f) * (1.0f - p);
            tgt = eye + fwd;
        } else if (mode == M_EXPLORE) {
            eye = pEye + vec3(0, 0.05f * std::sin(walkPh), 0);
            tgt = eye + fwd;
        } else if (mode == M_RETURN) {
            if (stateT < T_RSW) { eye = pEye + fwd * (2.5f * sm(stateT / T_RSW)); tgt = eye + fwd; }
            else {
                float p = sm((stateT - T_RSW) / (T_REND - T_RSW));
                eye = bezier(endEye, endEye + (hubEye - endEye) * 0.2f + vec3(0, 1.0f, 0), hubEye + (endEye - hubEye) * 0.25f + vec3(0, 2.5f, 0), hubEye, p);
                tgt = glm::mix(pc, hubTarget, p);
            }
        }
        mat4 view = glm::lookAt(eye, tgt, vec3(0, 1, 0));
        mat4 vp = proj * view;
        vec3 camRight(view[0][0], view[1][0], view[2][0]), camUp(view[0][1], view[1][1], view[2][1]);
        float pointScale = float(height) / (2.0f * std::tan(glm::radians(55.0f) * 0.5f));
        float pxScale = float(height) / 720.0f;
        const Palette& pal = PAL[sceneId + 1];
        float wa = sceneId >= 0 ? wActS[sceneId] : 0.0f;
        float flow = 1.0f - 2.0f * wActS[3];

        // manta (timed translation) - hub only
        vec3 mpos = {4.7f * std::cos(t * 0.55f), 3.7f + 0.4f * std::sin(t * 1.2f), 4.7f * std::sin(t * 0.55f)};
        mat4 mm = glm::translate(I, mpos) * glm::rotate(I, -t * 0.55f, vec3(0, 1, 0)) * glm::rotate(I, -0.22f, vec3(0, 0, 1)) * glm::rotate(I, 0.06f * std::sin(t * 1.2f), vec3(1, 0, 0));

        // ----- particles per environment
        if (sceneId < 0) {
            for (int i = 0; i < 2; ++i) {
                vec3 tail = vec3(mm * vec4(0.0f, 0.0f, -2.8f + prng.r(0, 0.3f), 1.0f));
                emit(tail, {prng.r(-.2f, .2f), prng.r(-.1f, .25f), prng.r(-.2f, .2f)}, vec3(0.5f, 1.4f, 1.8f), 1.8f, 0.14f);
            }
            accA += dt * 28.0f;
            while (accA > 1.0f) {
                accA -= 1.0f;
                float a = prng.r(0, 2 * PI), r = std::sqrt(prng.f()) * 6.6f;
                vec3 c = glm::mix(vec3(1.4f, 0.9f, 0.5f), glm::mix(vec3(1.0f, 0.5f, 1.5f), vec3(0.5f, 1.4f, 1.1f), prng.f()), prng.f());
                emit(vec3(r * std::cos(a), DECK_Y + 0.1f, r * std::sin(a)), {prng.r(-.12f, .12f), prng.r(.25f, .6f), prng.r(-.12f, .12f)}, c * 0.7f, 6.0f, 0.10f);
            }
            if (state.burst) {
                state.burst = false;
                for (int i = 0; i < 110; ++i) {
                    float z = prng.r(-1, 1), a = prng.r(0, 2 * PI), s2 = std::sqrt(1 - z * z);
                    vec3 d(s2 * std::cos(a), z, s2 * std::sin(a));
                    emit(coreCenter + d, d * prng.r(1.5f, 5.0f), vec3(2.6f, 1.7f, 0.7f), 1.6f, 0.16f);
                }
            }
        } else if (mode == M_EXPLORE || mode == M_ARRIVE || mode == M_RETURN) {
            accA += dt * 30.0f;
            while (accA > 1.0f) {
                accA -= 1.0f;
                float a = prng.r(0, 2 * PI), r = std::sqrt(prng.f()) * 11.0f;
                vec3 gp(r * std::cos(a), 0.2f, r * std::sin(a));
                if (sceneId == 0) {
                    vec3 c = glm::mix(vec3(0.4f, 1.4f, 1.0f), vec3(1.4f, 1.0f, 0.5f), prng.f());
                    emit(gp, {prng.r(-.1f, .1f), prng.r(.5f, 1.1f), prng.r(-.1f, .1f)}, c * (0.5f + 0.6f * wa), 7.0f, 0.10f, 1.0f);
                } else if (sceneId == 1) {
                    int ch = (int)(prng.f() * 6);
                    float cA = ch * PI / 3 + 0.4f, hh = 3.0f + (ch % 3) * 1.1f;
                    emit(vec3(10.4f * std::cos(cA), hh + 0.3f, 10.4f * std::sin(cA)), {prng.r(-.3f, .3f), prng.r(.8f, 1.8f), prng.r(-.3f, .3f)}, vec3(1.6f, 0.7f, 0.25f), 2.2f, 0.12f, 0.8f);
                } else if (sceneId == 2) {
                    if (wa > 0.05f) {
                        float oa = prng.r(0, 2 * PI);
                        vec3 c = glm::mix(vec3(1.0f, 0.7f, 1.5f), vec3(1.5f, 1.2f, 0.6f), prng.f());
                        emit(vec3(1.4f * std::cos(oa), 2.9f, 1.4f * std::sin(oa)), {-std::sin(oa) * 1.4f, prng.r(.3f, 1.0f), std::cos(oa) * 1.4f}, c, 3.0f, 0.09f, 0.9f);
                    } else emit(gp + vec3(0, 0.5f, 0), {0, 0.3f, 0}, vec3(0.8f, 0.6f, 1.2f) * 0.5f, 6.0f, 0.08f, 1.0f);
                } else {
                    vec3 c = glm::mix(vec3(0.6f, 1.0f, 1.6f), vec3(1.5f, 1.4f, 1.2f), prng.f());
                    float y = prng.r(1.0f, 22.0f);
                    emit(vec3(r * std::cos(a), y, r * std::sin(a)), {0, 1.2f * flow, 0}, c * 0.8f, 7.0f, 0.10f, 1.0f);
                }
            }
            if (sceneId == 1) {
                float mach = 0.15f + 0.85f * wa;
                accB += dt * 40.0f * mach;
                while (accB > 1.0f) {
                    accB -= 1.0f;
                    mat4 m1, m2; vec3 tip;
                    armPose((int)(prng.f() * 3), t, mach, m1, m2, tip);
                    emit(tip, {prng.r(-.5f, .5f), prng.r(-.5f, .2f), prng.r(-.5f, .5f)}, vec3(2.0f, 0.9f, 0.3f), 1.6f, 0.13f, 0.6f);
                }
            } else if (sceneId == 3) {
                accC += dt * 70.0f;
                while (accC > 1.0f) {
                    accC -= 1.0f;
                    emit(vec3(prng.r(-.2f, .2f), 11.9f, prng.r(-.2f, .2f)), {prng.r(-.15f, .15f), -3.4f * flow, prng.r(-.15f, .15f)}, vec3(1.8f, 1.5f, 0.9f), 2.6f, 0.09f, 1.0f);
                }
            }
        }
        if (mode == M_CROSS || mode == M_APPROACH) {
            accC += dt * (mode == M_CROSS ? 260.0f : 50.0f);
            while (accC > 1.0f && target >= 0) {
                accC -= 1.0f;
                float z = prng.r(-1, 1), a = prng.r(0, 2 * PI), s2 = std::sqrt(1 - z * z);
                vec3 d(s2 * std::cos(a), z, s2 * std::sin(a));
                emit(pc + d * pr, d * prng.r(0.8f, 3.0f), worldTint[target] * 2.2f, 1.3f, 0.14f);
            }
        }
        {
            VList pv;
            pv.reserve(parts.size());
            for (size_t i = 0; i < parts.size();) {
                Particle& p = parts[i];
                p.life -= dt;
                if (p.life <= 0) { parts[i] = parts.back(); parts.pop_back(); continue; }
                p.p += p.v * dt;
                p.v *= std::pow(p.drag, dt);
                if (sceneId >= 0 && p.p.y < -1.0f) p.life = 0.0f;
                float lf = p.life / p.maxLife;
                Vertex v; v.pos = p.p; v.col = p.c;
                v.aux = vec4(p.size * (0.4f + 0.6f * lf), float(i) * 0.37f, std::min(lf * 2.0f, 1.0f) * lf, 0);
                pv.push_back(v);
                ++i;
            }
            particleMesh.update(pv);
        }

        // =============================== SCENE PASS (HDR, MSAA)
        glBindFramebuffer(GL_FRAMEBUFFER, T.msFbo);
        glViewport(0, 0, T.w, T.h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        sky.use();
        setSky(sky, pal);
        sky.m4("uInvVP", glm::inverse(vp));
        sky.v3("uCam", eye);
        sky.f1("uTime", t);
        glBindVertexArray(emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // light inside the current environment
        vec3 lightPos = coreCenter;
        float lightI = 7.0f * (1.0f + 0.08f * std::sin(t * 1.7f));
        if (sceneId == 0) { lightPos = vec3(0, 6.8f, 0); lightI = 4.0f + 5.0f * wa; }
        else if (sceneId == 1) { lightPos = vec3(0, 6.9f + wa * 5.2f, 0); lightI = 5.0f + 11.0f * wa; }
        else if (sceneId == 2) { lightPos = vec3(0, 3.0f, 0); lightI = 5.0f + 10.0f * wa; }
        else if (sceneId == 3) { lightPos = vec3(0, 11.9f, 0); lightI = 34.0f; }

        scene.use();
        setSky(scene, pal);
        scene.m4("uViewProj", vp);
        scene.v3("uCam", eye);
        scene.v3("uCoreLight", lightPos);
        scene.v3("uCoreCol", pal.coreCol);
        scene.f1("uTime", t);
        scene.f1("uCoreI", lightI);

        if (sceneId < 0) {
            draw(deck, I, M_MARBLE);
            draw(rock, I, M_STONE);
            draw(brass, I, M_BRASS);
            draw(inlay, I, M_INLAY);
            draw(crystals, I, M_CRYSTAL);
            for (const Isle& is : isles) {
                vec3 p = is.pos + vec3(0, 0.6f * std::sin(t * 0.35f + is.bob), 0);
                draw(islands[is.mesh], glm::translate(I, p) * glm::rotate(I, is.bob + t * 0.01f, vec3(0, 1, 0)) * glm::scale(I, vec3(is.scale)), M_STONE);
            }
            const float aa = state.atlasAngle;
            draw(core, glm::translate(I, coreCenter), emissive(3.4f));
            mat4 rMat[3] = {
                glm::translate(I, coreCenter) * glm::rotate(I, aa, vec3(0, 1, 0)) * glm::rotate(I, 0.35f, vec3(1, 0, 0)),
                glm::translate(I, coreCenter) * glm::rotate(I, -aa * 1.35f, vec3(0, 1, 0)) * glm::rotate(I, 1.1f, vec3(1, 0, 0)),
                glm::translate(I, coreCenter) * glm::rotate(I, aa * 0.7f, vec3(0, 1, 0)) * glm::rotate(I, -0.8f, vec3(1, 0, 0)) * glm::rotate(I, 0.6f, vec3(0, 1, 0))};
            RingSet* rings[3] = {&outerRing, &middleRing, &innerRing};
            for (int i = 0; i < 3; ++i) {
                draw(rings[i]->body, rMat[i], M_BRASS);
                draw(rings[i]->beads, rMat[i], emissive(2.4f));
                draw(rings[i]->ticks, rMat[i], emissive(2.0f + (hoverAtlas ? 1.6f : 0.0f)));
            }
            mat4 worldM[4];
            for (int i = 0; i < 4; ++i) {
                mat4 pedM = glm::translate(I, vec3(wPos[i].x, 0, wPos[i].z));
                draw(pedestal, pedM, M_BRASS);
                draw(halo, pedM, emissive(2.6f + (i == selected ? 2.0f : 0.0f), worldTint[i]));
                worldM[i] = glm::translate(I, wPos[i]) * glm::scale(I, vec3(wScale[i]));
                draw(planets[i], worldM[i] * glm::rotate(I, t * 0.18f + i, vec3(0, 1, 0)) * glm::rotate(I, 0.25f, vec3(0, 0, 1)), M_PLANET);
            }
            draw(moon, worldM[0] * glm::rotate(I, t * 0.8f, vec3(0, 1, 0)) * glm::translate(I, vec3(1.15f, 0.15f, 0)), M_PLANET);
            draw(cogRing, worldM[1] * glm::rotate(I, 0.5f, vec3(1, 0, 0)) * glm::rotate(I, t * 0.5f, vec3(0, 1, 0)), M_BRASS);
            draw(saturnRing, worldM[2] * glm::rotate(I, 0.45f, vec3(1, 0, 0)) * glm::rotate(I, 0.2f, vec3(0, 0, 1)), M_PLANET);
            draw(moon, worldM[3] * glm::rotate(I, t * 1.1f, vec3(0, 1, 0)) * glm::translate(I, vec3(1.1f, 0, 0)), emissive(2.2f, ICE));
            draw(moon, worldM[3] * glm::rotate(I, -t * 0.7f + 2.0f, vec3(0.3f, 1, 0)) * glm::translate(I, vec3(1.3f, 0, 0)), emissive(2.2f, vec3(0.8f, 0.6f, 1.0f)));
            Mat mt = M_MANTA;
            mt.flap = 1.0f;
            draw(manta, mm, mt);
        } else {
            drawInterior(sceneId, t, wa, flow);
        }

        // stars
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        points.use();
        points.m4("uViewProj", vp);
        points.m4("uModel", I);
        points.f1("uTime", t);
        points.f1("uWorldSize", 0.0f);
        points.f1("uPx", pxScale);
        points.f1("uPointScale", pointScale);
        points.f1("uTwinkle", 1.0f);
        points.f1("uSwirl", 0.0f);
        starfield.draw();

        // cloud sea
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        bill.use();
        bill.m4("uViewProj", vp);
        bill.v3("uRight", camRight);
        bill.v3("uUp", camUp);
        bill.f1("uTime", t);
        bill.v3("uCloudTint", pal.cloud);
        clouds.draw();

        // additive glow pass
        glBlendFunc(GL_ONE, GL_ONE);
        scene.use();
        if (sceneId < 0) {
            for (const Gate& g : portals) draw(portalMesh, g.M, portalMat(g.tint));
            draw(corona, glm::translate(I, coreCenter), glass(3.2f + (hoverAtlas ? 3.0f : 0.0f), vec3(1.0f, 0.65f, 0.28f)));
            draw(raysM, glm::translate(I, coreCenter) * glm::rotate(I, t * 0.12f, vec3(0.2f, 1, 0.1f)), emissive(1.6f));
            for (int i = 0; i < 4; ++i) {
                bool hv = i == hover, sl = i == selected;
                mat4 wm = glm::translate(I, wPos[i]) * glm::scale(I, vec3(wScale[i] * ((hv || sl) ? 1.07f : 1.0f)));
                float e = 2.2f + (hv ? 2.4f : 0.0f) + (sl ? 2.2f + std::sin(t * 4.0f) : 0.0f);
                draw(shell, wm, glass(e, worldTint[i]));
                if (sl) draw(selRing, glm::translate(I, wPos[i]) * glm::scale(I, vec3(wScale[i])) * glm::rotate(I, t * 0.9f, vec3(0, 1, 0)) * glm::rotate(I, 0.5f, vec3(1, 0, 0)), emissive(3.2f, worldTint[i]));
            }
        } else {
            drawInteriorGlow(sceneId, t, wa, flow);
        }
        points.use();
        points.f1("uWorldSize", 1.0f);
        points.f1("uTwinkle", 0.0f);
        particleMesh.draw();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        // =============================== RESOLVE + BLOOM + COMPOSITE
        glBindFramebuffer(GL_READ_FRAMEBUFFER, T.msFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, T.resFbo);
        glBlitFramebuffer(0, 0, T.w, T.h, 0, 0, T.w, T.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(emptyVao);
        glActiveTexture(GL_TEXTURE0);
        glBindFramebuffer(GL_FRAMEBUFFER, T.bFbo[0]);
        glViewport(0, 0, T.bw, T.bh);
        bright.use();
        glBindTexture(GL_TEXTURE_2D, T.resTex);
        bright.i1("uTex", 0);
        bright.f1("uThreshold", 1.5f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        blur.use();
        blur.i1("uTex", 0);
        for (int i = 0; i < 6; ++i) {
            float spread = 1.0f + i * 0.45f;
            glBindFramebuffer(GL_FRAMEBUFFER, T.bFbo[1]);
            glBindTexture(GL_TEXTURE_2D, T.bTex[0]);
            blur.v2("uStep", vec2(spread / T.bw, 0));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindFramebuffer(GL_FRAMEBUFFER, T.bFbo[0]);
            glBindTexture(GL_TEXTURE_2D, T.bTex[1]);
            blur.v2("uStep", vec2(0, spread / T.bh));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        comp.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, T.resTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, T.bTex[0]);
        comp.i1("uScene", 0);
        comp.i1("uBloom", 1);
        comp.f1("uBloomK", state.bloom ? 0.55f : 0.0f);
        comp.f1("uFade", sstep(0.0f, 1.6f, introT));
        comp.f1("uTime", t);
        comp.f1("uPortal", veil);
        comp.v3("uPortalCol", veilCol);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE0);

        // =============================== HUD (labels, nameplates, prompts)
        {
            VList hv;
            const float px = std::max(2.0f, std::round(height / 400.0f));
            const vec3 cream(0.96f, 0.92f, 1.0f), gold(1.0f, 0.83f, 0.45f);
            float ha = (1.0f - veil) * sstep(1.5f, 3.0f, introT);
            const float fx = float(width) / winW, fy = float(height) / winH;
            auto plate = [&](float cx, float cy, const std::string& l1, const std::string& l2, vec3 accent) {
                float wdt = std::max(textWidth(l1, px), textWidth(l2, px * 0.8f)) + px * 10.0f, hgt = px * (l2.empty() ? 15.0f : 26.0f);
                float x = std::clamp(cx - wdt * 0.5f, 6.0f, width - wdt - 6.0f), y = std::clamp(cy - hgt, 6.0f, height - hgt - 6.0f);
                hudRect(hv, x, y, wdt, hgt, vec3(0.04f, 0.02f, 0.12f), 0.72f * ha);
                hudRect(hv, x, y, px * 1.5f, hgt, accent, 0.95f * ha);
                hudText(hv, l1, x + px * 5.0f, y + px * 4.0f, px, cream, ha);
                if (!l2.empty()) hudText(hv, l2, x + px * 5.0f, y + px * 15.0f, px * 0.8f, accent, 0.9f * ha);
            };
            auto toScreen = [&](vec3 p, float& sx, float& sy) {
                vec4 c = vp * vec4(p, 1.0f);
                if (c.w <= 0.0f) return false;
                sx = (c.x / c.w * 0.5f + 0.5f) * width;
                sy = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * height;
                return true;
            };
            if (sceneId < 0 && (mode == M_HUB)) {
                hudText(hv, "The Atlas of Impossible Worlds", px * 5, px * 5, px, gold, 0.85f * ha);
                hudText(hv, "Observatory", px * 5, px * 15, px, cream, 0.6f * ha);
                for (int pass = 0; pass < 2; ++pass) {
                    int i = pass == 0 ? selected : hover;
                    if (i < 0 || (pass == 1 && i == selected)) continue;
                    float sx, sy;
                    if (toScreen(wPos[i] + vec3(0, 0.95f * wScale[i], 0), sx, sy))
                        plate(sx, sy, WI[i].name, i == selected ? "[Enter] Enter world" : "Click to select", worldTint[i]);
                }
                if (hoverAtlas) plate(float(state.curX * fx) + px * 30, float(state.curY * fy) - px * 4, "The Atlas", "Click to rotate rings", gold);
                std::string hint = selected >= 0 ? "Enter / double-click: enter world | Scroll: scale | F: focus | Tab: next | R: reset"
                                                 : "Click planet: select | Click Atlas: rotate | Tab: cycle | Right-drag: orbit";
                if (selected >= 0) {
                    char sc[96];
                    std::snprintf(sc, sizeof(sc), "Selected: %s   scale %.2fx", WI[selected].name, wScale[selected]);
                    hudText(hv, sc, width * 0.5f, height - px * 30, px, worldTint[selected], 0.95f * ha, true);
                }
                hudText(hv, hint, width * 0.5f, height - px * 14, px, cream, 0.75f * ha, true);
            } else if (mode == M_EXPLORE) {
                float a2 = 1.0f;
                hudText(hv, WI[sceneId].name, px * 5, px * 5, px * 1.4f, gold, a2 * ha);
                std::string pr2 = WI[sceneId].prompt;
                pr2 += wAct[sceneId] > 0.5f ? "  [active]" : "";
                hudText(hv, pr2, width * 0.5f, height - px * 30, px, worldTint[sceneId], a2 * ha, true);
                hudText(hv, "WASD move | Right-drag look | Shift run | Esc / Backspace: return", width * 0.5f, height - px * 14, px, cream, 0.75f * a2 * ha, true);
            }
            if (mode == M_APPROACH || mode == M_CROSS || mode == M_ARRIVE || mode == M_RETURN) {
                if (mode == M_ARRIVE) hudText(hv, WI[sceneId].name, px * 5, px * 5, px * 1.4f, gold, sstep(0.3f, 0.9f, (stateT - T_CROSS) / (T_ARR - T_CROSS)));
                hudText(hv, "Space / Enter / Click: skip", width * 0.5f, height - px * 14, px, cream, 0.6f * (1.0f - veil * 0.7f), true);
                if (mode == M_APPROACH || mode == M_CROSS) hudText(hv, std::string("Entering ") + WI[target].name, width * 0.5f, px * 12, px * 1.6f, gold, 0.95f, true);
                if (mode == M_RETURN && stateT > T_RSW) hudText(hv, "Returning to the Observatory", width * 0.5f, px * 12, px * 1.6f, gold, 0.9f, true);
            }
            hudMesh.update(hv);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            hud.use();
            hud.v2("uRes", vec2(float(width), float(height)));
            hudMesh.draw();
            glDisable(GL_BLEND);
        }

        ++frameNo;
        if (!shotPath.empty() && frameNo == shotFrames) { saveTGA(shotPath.c_str(), width, height); break; }
        if (state.shot) {
            state.shot = false;
            static int shotIdx = 0;
            char name[64];
            std::snprintf(name, sizeof(name), "atlas_screenshot_%02d.tga", shotIdx++);
            saveTGA(name, width, height);
        }
        glfwSwapBuffers(w);

        ++fpsFrames;
        if (now - fpsTime > 0.5) {
            char title[160];
            std::snprintf(title, sizeof(title), "THE ATLAS OF IMPOSSIBLE WORLDS | Prototype  -  %.0f FPS", fpsFrames / (now - fpsTime));
            glfwSetWindowTitle(w, title);
            fpsTime = now;
            fpsFrames = 0;
        }
    }

    // ----------------------------------------------------------- cleanup
    Mesh* all[] = {&deck, &rock, &brass, &inlay, &crystals, &core, &corona, &raysM, &portalMesh, &pedestal, &halo, &moon,
                   &cogRing, &saturnRing, &shell, &manta, &starfield, &clouds, &particleMesh, &hudMesh, &selRing,
                   &planets[0], &planets[1], &planets[2], &planets[3], &islands[0], &islands[1], &islands[2],
                   &outerRing.body, &outerRing.ticks, &outerRing.beads, &middleRing.body, &middleRing.ticks, &middleRing.beads,
                   &innerRing.body, &innerRing.ticks, &innerRing.beads,
                   &gTree, &gOrbs, &fStem, &fHead, &fTower, &fGear[0], &fGear[1], &fGear[2], &fArm1, &fArm2,
                   &lShelves, &lBook, &lBird, &lConst, &lConstNodes, &kGlass, &kSandLow, &kSandUp, &kFrame, &kRail, &kPlank, &kClock,
                   &iRock[0], &iRock[1], &iRock[2], &iRock[3], &iTop[0], &iTop[1], &iTop[2], &iTop[3],
                   &iInlay[0], &iInlay[1], &iInlay[2], &iInlay[3]};
    for (Mesh* m : all) m->release();
    T.destroy();
    for (Prog* p : {&scene, &sky, &points, &bill, &bright, &blur, &comp, &hud}) glDeleteProgram(p->id);
    glDeleteVertexArrays(1, &emptyVao);
    glfwDestroyWindow(w);
    glfwTerminate();
    return 0;
}
