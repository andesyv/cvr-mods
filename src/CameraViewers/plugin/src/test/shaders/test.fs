// Raymarching / raytracing shader written by me
#version 430 core
#define PI 3.14159
#define MAX_STEPS 50
#define EPSILON 0.01
#define TIME_MULT 0.3
#define SMOOTHNESS 1.1
#define SCENE_INTERP 0.8
#define MAX_BOUNCES 3

layout(location = 0) in vec2 uv;

layout(location = 0) uniform mat4 MVP = mat4(1.0);
layout(location = 1) uniform float t = 0.0;

layout(location = 0) out vec4 fragColor;

float sdf(vec4 s, vec3 p) {
    return length(s.xyz - p) - s.w;
}

float weightedAbs(float a, float b, float k, float w) {
    float d = a - b;
    return d < 0.0 ? -pow(2.0, w) * d: pow(2.0, -w) * d;
}

// polynomial smooth min (k = 0.1);
float smin( float a, float b, float k)
{
    float h = max( k-abs(a-b), 0.0 )/k;
    return min( a, b ) - h*h*k*(1.0/4.0);
}

float smix(float a, float b, float t, float k) {
    float d = b - a;
    float h = 1.0/k;
    return (-2.0 * d + 2.0 * h + 1.0) * t * t * t + (3.0 * d - h - 2.0) * t * t + (1.0 - h) * t + a;
}

vec3 smix(vec3 a, vec3 b, float t, float k) {
    vec3 d = b - a;
    float h = 1.0/k;
    return (-2.0 * d + 2.0 * h + 1.0) * t * t * t + (3.0 * d - h - 2.0) * t * t + (1.0 - h) * t + a;
}

// http://viniciusgraciano.com/blog/smin/ - More smin explanation
float smin2(float a, float b, float k) {
    float d = abs(a-b);
    float u = max(k - d, 0.0)/k;
    return min(a,b) - u*u*k;
}

vec4 spheres[3] = vec4[3](
    vec4(0., 2.0, 0., 1.3),
    vec4(3.0, 0.5, -0.8, 2.0),
    vec4(-0.9, -2.3, -0.1, 2.3)
);

vec4 spheres2[2] = vec4[2](
    vec4(0.3, -1.2, 0.4, 1.6),
    vec4(-2.0, 1.3, 2.0, 2.4)
);

float rayPlane(vec3 ro, vec3 rd, vec3 n, vec3 p0)
{
	// assuming vectors are all normalized
	float denom = dot(n, -rd);
	if (0.0001 < denom) {
		vec3 p2r = ro - p0;
		return dot(p2r, n) / denom;
	}

	return -1.0;
}

bool raymarch(inout vec4 ro, inout vec4 rd, out vec3 normal, float interpolation)
{
  float farDist = rd.w;

  for (int i = 0; i < MAX_STEPS; ++i) {
        vec3 p = ro.xyz + ro.w * rd.xyz;

        float dist = farDist;
        float dist2 = farDist;
        vec3 diff = vec3(farDist);
        vec3 diff2 = vec3(farDist);

        for (int j = 0; j < 3; j++)
            dist = smin(dist, sdf(spheres[j], p), SMOOTHNESS);
        for (int j = 0; j < 2; j++)
            dist2 = smin(dist2, sdf(spheres2[j], p), SMOOTHNESS);

        dist = smix(dist, dist2, interpolation, SCENE_INTERP);
        // dist = mix(dist, dist2, interpolation);


        if (dist < EPSILON) {
            // Calculate gradient
            vec3 g = vec3(0.0);
            vec3 g2 = vec3(0.0);

            for (int j = 0; j < 3; j++) {
                diff[0] = smin(diff[0], sdf(spheres[j], vec3(p.x+EPSILON, p.y, p.z)), SMOOTHNESS);
                diff[1] = smin(diff[1], sdf(spheres[j], vec3(p.x, p.y+EPSILON, p.z)), SMOOTHNESS);
                diff[2] = smin(diff[2], sdf(spheres[j], vec3(p.x, p.y, p.z+EPSILON)), SMOOTHNESS);
            }
            for (int j = 0; j < 2; j++) {
                diff2[0] = smin(diff2[0], sdf(spheres2[j], vec3(p.x+EPSILON, p.y, p.z)), SMOOTHNESS);
                diff2[1] = smin(diff2[1], sdf(spheres2[j], vec3(p.x, p.y+EPSILON, p.z)), SMOOTHNESS);
                diff2[2] = smin(diff2[2], sdf(spheres2[j], vec3(p.x, p.y, p.z+EPSILON)), SMOOTHNESS);
            }

            // Interpolate diff the same way dist was interpolated
            diff = smix(diff, diff2, interpolation, SCENE_INTERP);
            g = diff - vec3(dist); // Forward differences

            normal = normalize(g);
            return true;
        }

        ro.w += dist;
        if (farDist < ro.w)
            return false;
    }
}

void main() {    
    vec4 near = MVP * vec4(uv, -1.0, 1.0);
    near /= near.w;
    vec4 far = MVP * vec4(uv, 1.0, 1.0);
    far /= far.w;


    vec4 ro = vec4(near.xyz, 0.0);
    vec4 rd = vec4((far - near).xyz, 1.0);
    rd.w = length(rd.xyz);
    // const float marchDistance = 300.0;
    // rd.w = marchDistance;
    rd.xyz /= rd.w;

    vec4 lightPos = MVP * vec4(0., 0., -1.0, 1.0);
    lightPos /= lightPos.w;

    float interpolation = sin(t * TIME_MULT) * sin(t * TIME_MULT);

    vec3 normal = vec3(0.0);
    vec3 color = vec3(0.0);
    vec3 p = ro.xyz;
    
    bool hitAnything = false;
    bool hitLast = false;
    for (int i = 0; i < 2; ++i) {
      bool marchHit = raymarch(ro, rd, normal, interpolation);
      float plane = rayPlane(ro.xyz, rd.xyz, vec3(0., 1., 0.), vec3(0., -5.4, 0.));

      if (!marchHit && plane <= 0.0)
        break;
      
      hitAnything = true;

      vec3 surfaceColor = vec3(0., 0.3, 0.8); //vec3(normal * 0.5 + 0.5);

      // Reset parameters to continue bounding from hit position
      
      // If plane is closer, probably hit plane
      if (0.0 < plane && (plane < ro.w || !marchHit)) {
        p = ro.xyz + plane * rd.xyz;
        normal = vec3(0., 1., 0.);
        surfaceColor = vec3(1.0, 0.0, 0.0);
      } else {
        p = ro.xyz + ro.w * rd.xyz;
      }

      vec3 lightDir = normalize(lightPos.xyz - p);
      float phong = max(dot(normal, lightDir), 0.15);
      float bounceMultiplier = pow(0.5, float(i));
      color = phong * vec3(normal * 0.5 + 0.5) * bounceMultiplier;

      rd.xyz = reflect(rd.xyz, normal);
      ro = vec4(p + rd.xyz * 0.3, 0.0);
    }

    if (hitAnything) {
      fragColor = vec4(color, 1.0);
      return;
    }


    fragColor = vec4(rd.xyz, 1.0);
}