#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(location = 0) in vec2 ST;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2DMS target;

//options are edge, colorEdge, or trueColorEdge
#define EDGE_FUNC trueColorEdge

//options are KAYYALI_NESW, KAYYALI_SENW, PREWITT, ROBERTSCROSS, SCHARR, or SOBEL
#define SOBEL

// Use these parameters to fiddle with settings
#ifdef SCHARR
#define STEP 0.15
#else
#define STEP 1.0
#endif


#ifdef KAYYALI_NESW
const mat3 kayyali_NESW = mat3(-6.0, 0.0, 6.0,
							   0.0, 0.0, 0.0,
							   6.0, 0.0, -6.0);
#endif
#ifdef KAYYALI_SENW
const mat3 kayyali_SENW = mat3(6.0, 0.0, -6.0,
							   0.0, 0.0, 0.0,
							   -6.0, 0.0, 6.0);
#endif
#ifdef PREWITT
// Prewitt masks (see http://en.wikipedia.org/wiki/Prewitt_operator)
const mat3 prewittKernelX = mat3(-1.0, 0.0, 1.0,
								 -1.0, 0.0, 1.0,
								 -1.0, 0.0, 1.0);

const mat3 prewittKernelY = mat3(1.0, 1.0, 1.0,
								 0.0, 0.0, 0.0,
								 -1.0, -1.0, -1.0);
#endif
#ifdef ROBERTSCROSS
// Roberts Cross masks (see http://en.wikipedia.org/wiki/Roberts_cross)
const mat3 robertsCrossKernelX = mat3(1.0, 0.0, 0.0,
									  0.0, -1.0, 0.0,
									  0.0, 0.0, 0.0);
const mat3 robertsCrossKernelY = mat3(0.0, 1.0, 0.0,
									  -1.0, 0.0, 0.0,
									  0.0, 0.0, 0.0);
#endif
#ifdef SCHARR
// Scharr masks (see http://en.wikipedia.org/wiki/Sobel_operator#Alternative_operators)
const mat3 scharrKernelX = mat3(3.0, 10.0, 3.0,
								0.0, 0.0, 0.0,
								-3.0, -10.0, -3.0);

const mat3 scharrKernelY = mat3(3.0, 0.0, -3.0,
								10.0, 0.0, -10.0,
								3.0, 0.0, -3.0);
#endif
#ifdef SOBEL
// Sobel masks (see http://en.wikipedia.org/wiki/Sobel_operator)
const mat3 sobelKernelX = mat3(1.0, 0.0, -1.0,
							   2.0, 0.0, -2.0,
							   1.0, 0.0, -1.0);

const mat3 sobelKernelY = mat3(-1.0, -2.0, -1.0,
							   0.0, 0.0, 0.0,
							   1.0, 2.0, 1.0);
#endif

//performs a convolution on an image with the given kernel
float convolve(mat3 kernel, mat3 image) {
	float result = 0.0;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			result += kernel[i][j]*image[i][j];
		}
	}
	return result;
}

//helper function for colorEdge()
float convolveComponent(mat3 kernelX, mat3 kernelY, mat3 image) {
	vec2 result;
	result.x = convolve(kernelX, image);
	result.y = convolve(kernelY, image);
	return clamp(length(result), 0.0, 255.0);
}

ivec2 norm_to_int(vec2 p) {
  return ivec2(round(p * vec2(textureSize(target))));
}

//returns color edges using the separated color components for the measure of intensity
//for each color component instead of using the same intensity for all three.  This results
//in false color edges when transitioning from one color to another, but true colors when
//the transition is from black to color (or color to black).
vec4 colorEdge(float stepx, float stepy, vec2 center, mat3 kernelX, mat3 kernelY, int s) {
  //get samples around pixel
  vec4 colors[9];
  colors[0] = texelFetch(target, norm_to_int(center + vec2(-stepx,stepy)), s);
  colors[1] = texelFetch(target, norm_to_int(center + vec2(0,stepy)), s);
  colors[2] = texelFetch(target, norm_to_int(center + vec2(stepx,stepy)), s);
  colors[3] = texelFetch(target, norm_to_int(center + vec2(-stepx,0)), s);
  colors[4] = texelFetch(target, norm_to_int(center), s);
  colors[5] = texelFetch(target, norm_to_int(center + vec2(stepx,0)), s);
  colors[6] = texelFetch(target, norm_to_int(center + vec2(-stepx,-stepy)), s);
  colors[7] = texelFetch(target, norm_to_int(center + vec2(0,-stepy)), s);
  colors[8] = texelFetch(target, norm_to_int(center + vec2(stepx,-stepy)), s);

  mat3 imageR, imageG, imageB, imageA;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      imageR[i][j] = colors[i*3+j].r;
      imageG[i][j] = colors[i*3+j].g;
      imageB[i][j] = colors[i*3+j].b;
      imageA[i][j] = colors[i*3+j].a;
    }
  }

  vec4 color;
  color.r = convolveComponent(kernelX, kernelY, imageR);
  color.g = convolveComponent(kernelX, kernelY, imageG);
  color.b = convolveComponent(kernelX, kernelY, imageB);
  color.a = convolveComponent(kernelX, kernelY, imageA);

  return color;
}

//finds edges where fragment intensity changes from a higher value to a lower one (or
//vice versa).
vec4 edge(float stepx, float stepy, vec2 center, mat3 kernelX, mat3 kernelY, int s) {
  // get samples around pixel
  mat3 image = mat3(length(texelFetch(target, norm_to_int(center + vec2(-stepx,stepy)), s).rgb),
      length(texelFetch(target, norm_to_int(center + vec2(0,stepy)), s).rgb),
      length(texelFetch(target, norm_to_int(center + vec2(stepx,stepy)), s).rgb),
      length(texelFetch(target, norm_to_int(center + vec2(-stepx,0)), s).rgb),
      length(texelFetch(target, norm_to_int(center), s).rgb),
      length(texelFetch(target, norm_to_int(center + vec2(stepx,0)), s).rgb),
      length(texelFetch(target, norm_to_int(center + vec2(-stepx,-stepy)), s).rgb),
      length(texelFetch(target, norm_to_int(center + vec2(0,-stepy)), s).rgb),
      length(texelFetch(target, norm_to_int(center + vec2(stepx,-stepy)), s).rgb));
  vec2 result;
  result.x = convolve(kernelX, image);
  result.y = convolve(kernelY, image);

  float color = clamp(length(result), 0.0, 255.0);

  return vec4(color);
}

//Colors edges using the actual color for the fragment at this location
vec4 trueColorEdge(float stepx, float stepy, vec2 center, mat3 kernelX, mat3 kernelY, int s) {
	vec4 edgeVal = edge(stepx, stepy, center, kernelX, kernelY, s);
	return edgeVal * texelFetch(target, norm_to_int(center), s);
}

void main() {
	vec2 uv = (gl_FragCoord.xy - vec2(0.5) + gl_SamplePosition.xy) / textureSize(target);
  outColor = vec4(0);
  for (int i = 0; i < textureSamples(target); i++) {
#ifdef KAYYALI_NESW
	outColor += EDGE_FUNC(STEP/textureSize(target)[0], STEP/textureSize(target)[1],
							uv,
							kayyali_NESW, kayyali_NESW, i);
#endif
#ifdef KAYYALI_SENW
	outColor += EDGE_FUNC(STEP/textureSize(target)[0], STEP/textureSize(target)[1],
							uv,
							kayyali_SENW, kayyali_SENW, i);
#endif
#ifdef PREWITT
	outColor += EDGE_FUNC(STEP/textureSize(target)[0], STEP/textureSize(target)[1],
							uv,
							prewittKernelX, prewittKernelY, i);
#endif
#ifdef ROBERTSCROSS
	outColor += EDGE_FUNC(STEP/textureSize(target)[0], STEP/textureSize(target)[1],
							uv,
							robertsCrossKernelX, robertsCrossKernelY, i);
#endif
#ifdef SOBEL
	outColor += EDGE_FUNC(STEP/textureSize(target)[0], STEP/textureSize(target)[1],
							uv,
							sobelKernelX, sobelKernelY, i);
#endif
#ifdef SCHARR
	outColor += EDGE_FUNC(STEP/textureSize(target)[0], STEP/textureSize(target)[1],
							uv,
							scharrKernelX, scharrKernelY, i);
#endif
  }

  outColor /= textureSamples(target);
}
