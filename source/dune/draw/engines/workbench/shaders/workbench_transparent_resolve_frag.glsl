void main()
{
  /* Revealage is actually stored in transparentAccum alpha channel.
   * This is a workaround to older hardware not having separate blend equation per render target.
   */
  vec4 trans_accum = texture(transparentAccum, uvcoordsvar.xy);
  float trans_weight = texture(transparentRevealage, uvcoordsvar.xy).r;
  float trans_reveal = trans_accum.a;

  /* Listing 4 */
  fragColor.rgb = trans_accum.rgb / clamp(trans_weight, 1e-4, 5e4);
  fragColor.a = 1.0 - trans_reveal;
}
