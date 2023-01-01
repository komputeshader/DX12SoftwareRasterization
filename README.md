# DX12SoftwareRasterization
This demo attempts to render scenes with big amount of triangles using compute shader rasterization. It also attempts to simulate game engine geometry load with cascaded shadows and culling.

It comes with two scenes, Buddha - about 100M of really small triangles, and Plant - about 40M of triangles of various sizes. Even with the most simplistic rasterization approach, software rasterizer outperforms hardware rasterizer on Buddha scene, but Plant scene is quite unstable for now in terms of performance, due to presence of alot of "big" triangles.

Demo attemps to distribute load over threads  with the notion of big triangle - how big the triangle's screen area should be to rasterize it with a single thread, or to offload it to multiple-threads rasterizer, or hardware rasterizer?

![Buddha scene](https://github.com/komputeshader/DX12SoftwareRasterization/blob/main/BuddhaScene.png)

# How to build and run:
1. Clone this repo with git clone https://github.com/komputeshader/DX12SoftwareRasterization.git.
2. Open .sln file with VS.
3. Build and run using VS.

# WIP:
* Top-left rasterization rule.
* More advanced rasterization algorithm.
* Material system and texturing support.
* Partial derivatives for software rasterizer.
* Per-triangle culling.
* Geometry meshletization and meshlets culling.
* Previous frame hi-z culling.

# Papers and other resources used:
* [A Parallel Algorithm for Polygon Rasterization](https://www.cs.drexel.edu/~david/Classes/Papers/comp175-06-pineda.pdf)
* [Optimizing the Graphics Pipeline with Compute](https://frostbite-wp-prd.s3.amazonaws.com/wp-content/uploads/2016/03/29204330/GDC_2016_Compute.pdf)
* Models downloaded from Morgan McGuire's [Computer Graphics Archive](https://casual-effects.com/data)
