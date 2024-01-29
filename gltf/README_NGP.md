# GLTF Neural Asset Formats (v0.4, *subject to change*)
A GLTF neural asset contains an extension named `ADOBE_nerf_asset`, which includes the following keys:

## Hash Grid

`hash_grid`: An `np.float16` 3D tensor storing the hash grid. It has the shape indicated by `hash_grid_shape`.

`hash_grid_shape`: An `int` vector stroing the shape of `hash_grid`. By default, the value is `[8, 524288, 4]`. The shape can be variable based on different versions of the neural asset.

`hash_grid_res`: An `int` list storing the resolutions of all the levels of the hash grid. Its length has to match the first dimension length of `hash_grid`, which is by default `8`, making the input feature containing `8` `vec4`-s. The resolutions are `[80, 117, 172, 254, 373, 549, 807, 1186]` by default.

## Spatial MLP

`spatial_mlp_l0_weight`: An `np.float32` vector storing the level-0 weight of the MLP to infer the spatial vector, with its length specified by `spatial_mlp_l0_weight_shape`. 

`spatial_mlp_l0_weight_shape`: An `int` vector storing the length of `spatial_mlp_l0_weight`. By default, the value is `768`, corresponding to a 2D tensor in shape `(32, 24)`. Note that the first dimension here matches the dimension of the input feature defined through `hash_grid_res`. The data of `spatial_mlp_l0_weight` is packed in the order corresponding to a row-major 4D tensor in shape `(8, 6, 4, 4)`, or 48 row-major 4x4 matrices.

`spatial_mlp_l0_bias`: An `np.float32` vector storing the level-0 bias of the MLP to infer the spatial vector, with its length specified by `spatial_mlp_l0_bias_shape`.

`spatial_mlp_l0_bias_shape`: An `int` vector storing the length of `spatial_mlp_l0_bias`. By default, the value is `24`, matching the inner dimension of the tensor represented by `spatial_mlp_l0_weight`.

`spatial_mlp_l1_weight`: An `np.float32` vector storing the level-1 weight of the MLP to infer the spatial vector, with its length specified by `spatial_mlp_l1_weight_shape`. 

`spatial_mlp_l1_weight_shape`: An `int` vector storing the length of `spatial_mlp_l1_weight`. By default, the value is `384`, corresponding to a 2D tensor in shape `(24, 16)`. The data of `spatial_mlp_l1_weight` is packed in the order corresponding to a row-major 4D tensor in shape `(6, 4, 4, 4)`, or 24 row-major 4x4 matrices. 

`spatial_mlp_l1_bias`: An `np.float32` storing the level-1 bias of the MLP to infer the spatial vector, with its length specified by `spatial_mlp_l1_bias_shape`.

`spatial_mlp_l1_bias_shape`: An `int` vector storing the length of `spatial_mlp_l1_bias`. By default, the value `16`, matching the inner dimension of the tensor represented by `spatial_mlp_l1_weight`.

## View-dependent MLP

`vdep_mlp_l0_weight`: An `np.float32` vector storing the level-0 weight of the MLP to infer the view-dependent color, with its length specified by `vdep_mlp_l0_weight_shape`. 

`vdep_mlp_l0_weight_shape`: An `int` vector storing the length of `vdep_mlp_l0_weight`. By default, the value is `864`, corresponding to a 2D tensor in shape `(36, 24)`. The `36`-dimension input feature fed into the layer represented by `vdep_mlp_l0_weight` concatenate the last `12` (of `16`) dimensions of the spatial vector (this is because the first `4` dimensions were used for density and diffuse color computation) and `24` dimensions of the trigonometrically-encoded viewing direction. The data of `vdep_mlp_l0_weight` is packed in the order corresponding to a row-major 4D tensor in shape `(9, 6, 4, 4)`, or 54 row-major 4x4 matrices. 

`vdep_mlp_l0_bias`: An `np.float32`  storing the level-0 bias of the MLP to infer the view-dependent color, with its length specified by `vdep_mlp_l0_bias_shape`.

`vdep_mlp_l0_bias_shape`: An `int` vector storing the length of `vdep_mlp_l0_bias`. By default, the value is `24`, matching the inner dimension of the tensor represented by `vdep_mlp_l0_weight`.

`vdep_mlp_l1_weight`: An `np.float32`  storing the level-1 weight of the MLP to infer the view-dependent color, with its length specified by `vdep_mlp_l1_weight_shape`. 

`vdep_mlp_l1_weight_shape`: An `int` vector storing the length of `vdep_mlp_l1_weight`. By default, the value is `576`, corresponding to a 2D tensor in shape `(24, 24)`. The data of `vdep_mlp_l1_weight` is packed in the order corresponding to a row-major 4D tensor in shape `(6, 6, 4, 4)`, or 36 row-major 4x4 matrices. 

`vdep_mlp_l1_bias`: An `np.float32`  storing the level-1 bias of the MLP to infer the view-dependent color, with its length specified by `vdep_mlp_l1_bias_shape`.

`vdep_mlp_l1_bias_shape`: An `int` vector storing the length of `vdep_mlp_l1_bias`. By default, the value is `24`, matching the inner dimension of the tensor represented by `vdep_mlp_l1_weight`.

`vdep_mlp_l2_weight`: An `np.float32` vector storing the level-2 weight of the MLP to infer the view-dependent color, with its length specified by `vdep_mlp_l2_weight_shape`. 

`vdep_mlp_l2_weight_shape`: An `int` vector storing the length of `vdep_mlp_l2_weight`. By default, the value is `96`, corresponding to a 2D tensor in shape `(24, 4)`. The data of `vdep_mlp_l2_weight` is packed in the order corresponding to a row-major 4D tensor in shape `(6, 1, 4, 4)`, or 6 row-major 4x4 matrices.

`vdep_mlp_l2_bias`: An `np.float32` storing the level-2 bias of the MLP to infer the view-dependent color, with its length specified by `vdep_mlp_l2_bias_shape`.

`vdep_mlp_l2_bias_shape`: An `int` vector storing the length of `vdep_mlp_l2_bias`. By default, the value is `4`, matching the inner dimension of the tensor represented by `vdep_mlp_l2_weight`.

## Unpack the MLP weights
Notice that both `spatial_mlp_l[0,1]_weight` and `vdep_mlp_l[0,1,2]_weight` are packed into 4x4 matrices for the ease of vectorized multiplication. 
One may want to reorder them for some applications to row-major 2D tensors. A simple code snippet to achieve this purpose would be like the following:
```
def reshape_MLP_weights(layer, d1, d2):
    tmp = np.reshape(layer.astype(np.float32), (d1//4, d2//4, 4, 4))
    out = np.transpose(tmp, (0, 2, 1, 3))
    return out.reshape(d1, d2)
```

## Distance Grid

`distance_grid`: An `np.uint8` 3D tensor, storing the square root of distance to the object surface in the range of `[0, 255]`. In a shader, this value is interpreted as a fixed-point number in the `[0, 1]` range. Its shape is specified by `distance_grid_shape`.

`distance_grid_shape`: An `int` vector storing the shape of `distance_grid`. The default value is `[128, 128, 128]`.

`distance_max`: An `np.float32` storing the scale of distance. The actual distance value is `distance_max` multiplied by the sampled fixed-point number from `distance_grid` squared.

## Density Grid

`density`: An `np.uint8` 3D tensor, storing the density value in the range of `[0, 255]`. This value is interpreted as a fixed-point number in the `[0, 1]` range in a shader. Its shape is specified by `density_shape`. The value in this tensor replicates (in a less precise format) the 0-th output we get from evaluating the spatial MLP at each grid node, and can be used to skip evaluating the MLP for fast importance sampling. Evaluating the spatial MLP directly during importance sampling should give similar results but can be much more costly.

`density_shape`: An `int` vector storing the shape of `density`. The default value is `[512, 512, 512]`.

`density_max`: An `np.float32` storing the scale of density. The actual density value is `density_max` multiplied by the sampled fixed-point number from `density`.

`sigma_threshold`: An `np.float32`, when the exponential density value is greater than it, the position where the density value is sampled is considered to be occluded. The value is `sqrt(25.0 / 3.0)` by default.

## Optional
The following fields are optional.

### Generic
`model_type`: A `string`, indicating the type of this neural asset. It is `ngp` by default, storing Neural Graphics Primitives (NGP).

`version`: A `string` storing the version of the GLTF asset. By default, it is `0.4`.

`bbox_min_xzy`: An `np.float32` vector indicating the minimal spatial coordinate of the model. It is `[-1, -1, -1]` by default.

`bbox_max_xzy`: An `np.float32` vector indicating the maximal spatial coordinate of the model. It is `[1, 1, 1]` by default.

### Camera

`camera_dist_minmax`: An `np.float32` vector indicating the min/max restriction on the camera radius. By default, it is `[1.0, 4.0]`.

`camera_dist`: An `np.float32`, indicating the default camera radius. By default, it is `2.0`.

`camera_elev_minmax`: An `np.float32` vector indicating the min/max restriction on the camera elevation angle. By default, it is `[0.0, 75.0]`.

`camera_elev`: An `np.float32` indicating the default camera elevation angle. By default, it is `45.0`.

`camera_azim_minmax`: An `np.float32` vector indicating the min/max restriction on the camera azimuth angle. By default, it is `[0.0, 360.0]`.

`camera_azim`: An `np.float32` indicating the default camera azimuth angle. By default, it is `315.0`.

`camera_lookat_xyz`: An `np.float32` vector indicating the look-at position of the camera. By default, it is `[0, 0, 0]`.

### Color Management

`background_color`: An `np.float32` vector indicating the background color. It is `[1, 1, 1]` by default.

`exposure`: An `np.float32` indicating the exposure used in rendering. By default, it is `0`.

`gamma`: An `np.float32` indicating the gamma correction used in rendering. By default, it is `2.2`.

`color_temperature`: An `np.float32` indicating the color temperature of the model. By default, it is `6500.0`.

### Mesh

The following fields can be empty, indicating there is no reconstructed mesh available along side with the asset.

`mesh_verts`: An `np.float16` 2D tensor storing the vertices of a coarse mesh reconstructed from the model. Its shape is specified by `mesh_verts_shape`.

`mesh_verts_shape`: An `int` vector storing the shape of `mesh_verts`, i.e., `[number of vertices, 3]`.

`mesh_faces`: An `np.int32` 2D tensor storing the indices of a coarse mesh reconstructed from the model. Its shape is specified by `mesh_faces_shape`.

`mesh_faces_shape`: An `int` vector storing the shape of `mesh_faces`, i.e., `[number of facets, 3]`,

### Options

`split_diffuse_vdep`: A `boolean` indicating whether the model uses a split between diffuse and view-dependent effects. By default, it is `True`.

### Magic Number

`warp_bound`: An `np.float32` indicating the warping coefficient to calculate the hashed position from a regular sampled position. By default, it is `1.0`.

`spatial_mlp_layer_num`: An `int` indicating the number of MLP layers to infer the spatial vector. By default, the value is `2`.

`vdep_mlp_layer_num`: An `int` indicating the number of MLP layers to infer the view-dependent color. By default, the value is `3`.

`viewdir_pos_freq`: An `int` indicating the number of frequencies to encode the 3D view direction vector. By default, the value is `4`. For each frequency, there are `6` values (sine and cosine of each dimension of the 3D vector), making the trigonometrically-encoded viewing direction have dimension `24` (related to `vdep_mlp_l0_weight_shape`).

## Up direction

In our assets, the up direction is assumed to be the Z-axis. Applications using the Y-axis as the up direction need to apply 90 degree rotation on the X-axis (i.e., `(x, y, z)` to `(x, -z, y)`) of their camera-related variables before sampling in the asset or to apply -90 degree rotation on the X-axis to any positional data (e.g., the look-at position, or the mesh) in the asset. 
