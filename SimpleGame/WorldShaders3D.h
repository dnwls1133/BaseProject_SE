#pragma once

namespace WorldShaders3D
{
inline constexpr char Vertex[] = R"GLSL(#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 surface;
layout(location = 3) in mat4 model;
layout(location = 7) in vec4 tint;
layout(location = 8) in float instanceMaterial;
layout(location = 9) in vec4 instanceClip;

uniform mat4 viewProjection;
uniform mat4 lightMatrix;
uniform float time;

out vec3 worldPosition;
out vec3 worldNormal;
out vec4 color;
out vec4 shadowPosition;
flat out vec4 clipBounds;
flat out int material;

void main()
{
    clipBounds = instanceClip;
    material = int(instanceMaterial < 0.0 ? surface.w : instanceMaterial);
    vec4 world = model * vec4(position, 1.0);
    if (material == 7)
    {
        world.y += sin(world.x * 2.1 + time * 1.7) * cos(world.z * 1.6 - time) * .025;
    }
    if (material == 8)
    {
        world.x += sin(time * 5.0 + world.z * 4.0) * max(position.y, 0.0) * .12;
        world.y += sin(time * 7.0 + world.x * 3.0) * max(position.y, 0.0) * .16;
    }
    worldPosition = world.xyz;
    worldNormal = normalize(transpose(inverse(mat3(model))) * normal);
    color = vec4(surface.rgb, 1.0) * tint;
    shadowPosition = lightMatrix * world;
    gl_Position = viewProjection * world;
}
)GLSL";

inline constexpr char Shadow[] = R"GLSL(#version 330 core
in vec4 color;
flat in int material;

void main()
{
    if (material == 7 || material == 8 || material == 11 || color.a < .8)
    {
        discard;
    }
}
)GLSL";

inline constexpr char Fragment[] = R"GLSL(#version 330 core
in vec3 worldPosition;
in vec3 worldNormal;
in vec4 color;
in vec4 shadowPosition;
flat in vec4 clipBounds;
flat in int material;

uniform sampler2DShadow shadowMap;
uniform vec3 eye;
uniform vec3 focus;
uniform int lampCount;
uniform vec3 lampPosition[12];
uniform vec3 lampColor[12];
uniform float time;
out vec4 outputColor;

float noise(vec3 p)
{
    return fract(sin(dot(floor(p), vec3(12.9, 78.2, 37.7))) * 43758.5);
}

void main()
{
    if (any(lessThan(gl_FragCoord.xy, clipBounds.xy)) ||
        any(greaterThanEqual(gl_FragCoord.xy, clipBounds.zw)))
    {
        discard;
    }
    // Dithered occluders retain depth correctness without painter sorting.
    if (color.a < .99 && fract(dot(floor(gl_FragCoord.xy), vec2(.754877, .56984))) > color.a)
    {
        discard;
    }
    vec3 n = normalize(worldNormal);
    vec3 lightDirection = normalize(vec3(-.55, 1.0, -.35));
    if (material == 7)
    {
        n = normalize(n + vec3(sin(worldPosition.x * 3.0 + time) * .18, 0.0,
                               cos(worldPosition.z * 3.0 - time) * .18));
    }
    float grainFrequency = material == 4 ? 32.0 : 10.0;
    float grainFade = 1.0 - smoothstep(.2, .8, length(fwidth(worldPosition * grainFrequency)));
    vec3 base = color.rgb * (.95 + .08 * (noise(worldPosition * grainFrequency) - .5) * grainFade);
    if (material == 1 || material == 5)
    {
        vec2 plane = abs(n.y) > .5 ? worldPosition.xz :
                     (abs(n.x) > .5 ? worldPosition.zy : worldPosition.xy);
        vec2 grout = abs(fract(plane * 1.5) - .5);
        float filterWidth = max(fwidth(plane.x), fwidth(plane.y)) * 1.5;
        float mortar = smoothstep(.025, .045 + filterWidth, min(grout.x, grout.y));
        base *= mix(.70, 1.0, mortar);
    }
    if (material == 3)
    {
        float grain = worldPosition.y * 24.0 + sin(worldPosition.x * 3.0);
        base *= .92 + .08 * sin(grain) * (1.0 - smoothstep(.3, 1.5, fwidth(grain)));
    }
    vec3 sc = shadowPosition.xyz / shadowPosition.w * .5 + .5;
    float visibility = 1.0;
    if (all(greaterThan(sc, vec3(0.0))) && all(lessThan(sc, vec3(1.0))))
    {
        float bias = max(.00012, .00065 * (1.0 - max(dot(n, lightDirection), 0.0)));
        vec2 stepSize = vec2(.7 / 2048.0);
        float depth = sc.z - bias;
        float filtered = texture(shadowMap, vec3(sc.xy + vec2(-stepSize.x, -stepSize.y), depth));
        filtered += texture(shadowMap, vec3(sc.xy + vec2(stepSize.x, -stepSize.y), depth));
        filtered += texture(shadowMap, vec3(sc.xy + vec2(-stepSize.x, stepSize.y), depth));
        filtered += texture(shadowMap, vec3(sc.xy + stepSize, depth));
        visibility = .22 + .78 * filtered * .25;
    }
    vec3 lighting = vec3(.24, .29, .32) +
                    vec3(.88, .81, .68) * max(dot(n, lightDirection), 0.0) * visibility;
    for (int i = 0; i < lampCount; ++i)
    {
        vec3 d = lampPosition[i] - worldPosition;
        float distance2 = dot(d, d);
        if (distance2 < 100.0)
        {
            vec3 direction = d * inversesqrt(max(distance2, .0001));
            float falloff = 1.0 - smoothstep(36.0, 100.0, distance2);
            lighting += lampColor[i] * max(dot(n, direction), 0.0) * falloff / (1.0 + distance2 * .22);
        }
    }
    vec3 result = base * lighting;
    // Restrained metal/blood sheen distinguishes equipment from rough cloth and stone.
    if (material == 12 || material == 9)
    {
        vec3 halfVector = normalize(lightDirection + normalize(eye - worldPosition));
        float power = material == 9 ? 40.0 : 22.0;
        result += vec3(.32, .30, .26) * pow(max(dot(n, halfVector), 0.0), power) * visibility;
    }
    if (material == 4)
    {
        result += base * .09 * pow(1.0 - max(dot(n, normalize(eye - worldPosition)), 0.0), 2.0);
    }
    if (material == 7)
    {
        result += pow(max(dot(reflect(-lightDirection, n),
                             normalize(eye - worldPosition)), 0.0), 32.0) * .4;
    }
    if (material == 8 || material == 11)
    {
        result = base * (1.7 + .35 * sin(time * 5.0 + worldPosition.y * 4.0));
    }
    if (material == 9)
    {
        result += vec3(.09, .008, .02) * (1.0 + sin(time + worldPosition.x));
    }
    float fog = smoothstep(18.0, 62.0, length(worldPosition.xz - focus.xz)) * .68;
    outputColor = vec4(mix(result, vec3(.055, .073, .078), fog), 1.0);
}
)GLSL";
} // namespace WorldShaders3D
