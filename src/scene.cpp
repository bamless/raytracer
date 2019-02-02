#include "scene.hpp"
#include "vec3f.hpp"
#include "ray.hpp"

#include <cmath>
#include <algorithm>

void Scene::addShape(std::unique_ptr<Shape>&& s) {
	this->shapes.push_back(std::move(s));
}

void Scene::addLight(std::unique_ptr<Light>&& l) {
	this->lights.push_back(std::move(l));
}

const std::vector<std::unique_ptr<Shape>>& Scene::getShapes() const {
	return this->shapes;
}

const std::vector<std::unique_ptr<Light>>& Scene::getLights() const {
	return this->lights;
}

void Scene::render(Image &img, int fov, int aaSamples) {
	float fovScale = tan((float) fov / 2 * M_PI / 180);
	float aspectRatio = (float) img.width / (float) img.height;

	#pragma omp parallel for schedule(dynamic) collapse(2)
	for(std::size_t j = 0; j < img.height; j++) {
		for(std::size_t i = 0; i < img.width; i++) {

			Vec3f color;
			for(int sj = -aaSamples/2; sj <= aaSamples/2; sj++) {
				for(int si = -aaSamples/2; si <= aaSamples/2; si++) {
					float x = (2 * (i + (si + 0.5) / (aaSamples + 1)) / img.width - 1) * fovScale;
					float y = (1 - 2 * (j + (sj + 0.5) / (aaSamples + 1)) / img.height) * 1 / aspectRatio * fovScale;

					Vec3f rayDir(x, y, -1);
					rayDir.norm();

					Ray ray(Vec3f(0, 0, 0), rayDir);

					color += trace(ray, 0);
				}
			}

			color /= (aaSamples + 1) * (aaSamples + 1);
			img.setPixel(i, j, float3ToRGB8(color));
		}
	}
}

static Vec3f computeDiffuse(const Vec3f& normal, const Vec3f& toLightVec,
	            const Vec3f& lightColor, const Material& m, float intensity) {
	float nDotl = normal.dot(toLightVec);
	float brightness = std::max(0.f, nDotl * intensity);
	return lightColor * m.color * brightness;
}

static Vec3f computeSpecular(const Vec3f& normal, const Vec3f& lightDir,
	const Vec3f& hitP, const Vec3f& lightColor, const Material& m, float intensity) {

	Vec3f toCamVec = -hitP;
	toCamVec.norm();

	Vec3f refLightDir = lightDir.reflect(normal);
	float specularFac = std::max(0.f, refLightDir.dot(toCamVec));
	float dampedFac = powf(specularFac, m.shineDamper);

	return lightColor * m.color * (dampedFac * m.shineness * intensity);
}

Vec3f Scene::trace(const Ray& r, int depth) {
	Intersection in;
	if(intersect(in, r)) {
		Vec3f hitPoint = r.pointAtDistance(in.dHit);
		Vec3f normal = in.hObj->normalAtPoint(hitPoint);
		Material& hMat = in.hObj->getMaterial();

		Vec3f color(AMBIENT);
		for(auto& l : lights) {
			Vec3f lightDir = l->directionAtPoint(hitPoint);
			Vec3f toLightVec = -lightDir;
			toLightVec.norm();

			Ray shadowRay(hitPoint, toLightVec, l->pointDistance(hitPoint));
			bool visible = !intersect(in, shadowRay);

			if(visible) {
				float intensity = l->intensityAtPoint(hitPoint);

				Vec3f diff = computeDiffuse(normal, toLightVec, l->getColor(), hMat, intensity);
				Vec3f spec = computeSpecular(normal, lightDir, hitPoint, l->getColor(), hMat, intensity);
				color += spec + diff;
			}
		}

		if(hMat.isReflective && depth < MAX_DEPTH) {
			Ray reflRay(hitPoint, r.getDirection().reflect(normal));
			color += trace(reflRay, depth + 1) * 0.38; // for now reflectivity is hardcoded
		}

		return color;
	}

	return Vec3f(0);
}

bool Scene::intersect(Intersection& in, const Ray& r) {
	in.hObj = nullptr;
	in.dHit = INFINITY;

	for(auto& s : shapes) {
		float d = s->intersect(r);
		if(d >= 0 && d < in.dHit) {
			in.hObj = s.get();
			in.dHit = d;
		}
	}

	return in.hObj != nullptr;
}
