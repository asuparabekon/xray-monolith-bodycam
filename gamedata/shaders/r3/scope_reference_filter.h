#ifndef SCOPE_REFERENCE_FILTER_H
#define SCOPE_REFERENCE_FILTER_H
Texture2D scope_reference_filter_source;
SamplerState scope_reference_filter_sampler;
float2 scope_reference_filter_texel;
float scope_reference_filter_threshold;
float scope_reference_filter_range;
float scope_reference_filter_contrast;
int scope_reference_filter_mode;
static const float3 scope_reference_filter_luma=float3(.212639,.715169,.072192);
float2 scope_reference_filter_bounded(float2 p){return clamp(p,scope_reference_filter_texel*.5,1.-scope_reference_filter_texel*.5);}
float3 scope_reference_filter(float2 vUv)
{
 float2 texel=scope_reference_filter_texel;
 int mode=scope_reference_filter_mode;
 float threshold=scope_reference_filter_threshold,thresholdRange=scope_reference_filter_range,contrast=scope_reference_filter_contrast;
 float3 sum=float3(0.,0.,0.);float weight=0.;
 if(mode==0){
  // 64 spatial bright samples, original Rec709/clamped linear threshold and
  // final x + boost*(x^1.75 - .5*x). Disk locations follow a Vogel spiral.
  for(int i=0;i<64;i++){
   float n=float(i)+.5,a=n*2.39996323;
   float2 p=scope_reference_filter_bounded(vUv+float2(cos(a),sin(a))*sqrt(n/64.)*texel*3.);
   float3 c=scope_reference_filter_source.SampleLevel(scope_reference_filter_sampler,p,0).rgb;
   sum+=c*clamp((dot(c,scope_reference_filter_luma)-threshold)/max(thresholdRange,.0001),0.,1.);
  }
  sum=max(sum/64.,float3(0.,0.,0.));sum+=contrast*(pow(sum,float3(1.75,1.75,1.75))-.5*sum);
 }else if(mode==3){
  float2 radial=vUv-.5;float radius=length(radial);float2 direction=radial/max(radius,.0001),tangent=float2(-direction.y,direction.x);
  float strength=smoothstep(.15,.5,radius);
  // Radial/tangential luminance-weighted dispersion, not just RGB offsets.
  for(int i=0;i<32;i++){
   float a=(float(i)+.5)*2.39996323,r=sqrt((float(i)+.5)/32.);
   float2 offset=(direction*cos(a)*.002+tangent*sin(a)*.01)*r*strength;
   float3 c=scope_reference_filter_source.SampleLevel(scope_reference_filter_sampler,scope_reference_filter_bounded(vUv+offset),0).rgb;float w=1.+dot(c,scope_reference_filter_luma)*.25;
   sum+=c*w;weight+=w;
  }
  sum/=weight;
 }else{
  float2 axis=mode==1?float2(texel.x,0.):float2(0.,texel.y);
  for(int i=-4;i<=4;i++){float w=1.-abs(float(i))/5.;sum+=scope_reference_filter_source.SampleLevel(scope_reference_filter_sampler,scope_reference_filter_bounded(vUv+axis*float(i)),0).rgb*w;weight+=w;}
  sum/=weight;
 }
 
 return max(sum,float3(0,0,0));
}
#endif
