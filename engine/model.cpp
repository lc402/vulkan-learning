#include "app.h"
#include "model.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE
#define STBI_MSC_SECURE_CRT
#include <tiny_gltf.h>

tinygltf::Model * qb::ModelMgr::getGltf(const std::string name){
	// read gltf from cache
	auto it = _gltfMap.find(name);
	if (it != _gltfMap.end())
		return it->second;
	std::string err;
	std::string warn;
	tinygltf::Model* model = new tinygltf::Model();
	bool ret = _loader.LoadASCIIFromFile(model, &err, &warn, name);
	if (!warn.empty()) {
		log_warning("%s", warn.c_str());
	}
	if (!err.empty()) {
		log_error("%s", err.c_str());
	}
	if (!ret) {
		log_error("failed to load gltf: %s", name.c_str());
	}
	_gltfMap.insert({ name, model });
	return _gltfMap.at(name);
}
qb::Model * qb::ModelMgr::getModel(const std::string name) {
	// read model from cache
	auto it = _modelMap.find(name);
	if (it != _modelMap.end())
		return it->second;
	Model* model = new Model();
	model->init(app, name);
	_modelMap.insert({ name, model });
	return _modelMap.at(name);
}
void qb::ModelMgr::init(App *app) {
	this->app = app;
}


void qb::ModelMgr::destroy() {
	for (auto& it : _modelMap) {
		it.second->destroy();
		delete it.second;
	}
}

void qb::Model::_loadNode(qb::Model::Node * parent, const tinygltf::Node & node, uint32_t nodeIndex){
	qb::Model::Node* newNode = new Node{};
	newNode->index = nodeIndex;
	newNode->parent = parent;
	newNode->name = node.name;
	newNode->skinIndex = node.skin;
	newNode->mat = glm::mat4(1.0f);

	// local node mat
	glm::vec3 tranlation = glm::vec3(0.0f);
	if (node.translation.size() == 3) {
		tranlation = glm::make_vec3(node.translation.data());
		newNode->translation = tranlation;
	}
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	if (node.rotation.size() == 4) {
		glm::quat q = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
		newNode->rotation = q;
	}
	glm::vec3 scale = glm::vec3(1.0f);
	if (node.scale.size() == 3) {
		scale = glm::make_vec3(node.scale.data());
		newNode->scale = scale;
	}
	if (node.matrix.size() == 16) {
		newNode->mat = glm::make_mat4x4(node.matrix.data());
	}

	// node with children
	if (node.children.size() > 0) {
		for (size_t i = 0; i < node.children.size(); i++) {
			_loadNode(newNode, gltf->nodes[node.children[i]], node.children[i]);
		}
	}

	// node contains mesh data
	if (node.mesh > -1) {
		const tinygltf::Mesh mesh = gltf->meshes[node.mesh];
		qb::Model::Mesh* newMesh = new Mesh(newNode->mat);
		for (size_t j = 0; j < mesh.primitives.size(); j++) {
			const tinygltf::Primitive &primitive = mesh.primitives[j];
			if (primitive.indices < 0) {
				continue;
			}
			uint32_t indexStart = static_cast<uint32_t>(indices.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertices.size());
			uint32_t indexCount = 0;
			bool hasSkin = false;
			// vertices
			{
				const float* bufferPos = nullptr;
				const float* bufferNormals = nullptr;
				const float* bufferTexCoords = nullptr;
				const uint16_t* bufferJoints = nullptr;
				const float* bufferWeights = nullptr;

				// position
				assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

				const tinygltf::Accessor &posAccessor = gltf->accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::BufferView &posView = gltf->bufferViews[posAccessor.bufferView];
				bufferPos = reinterpret_cast<const float*>(&(gltf->buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));

				// normal
				if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
					const tinygltf::Accessor &normalAccessor = gltf->accessors[primitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView &normalView = gltf->bufferViews[normalAccessor.bufferView];
					bufferNormals = reinterpret_cast<const float*>(&(gltf->buffers[normalView.buffer].data[normalAccessor.byteOffset + normalView.byteOffset]));
				}

				// skinning
				// joints
				if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor &jointAccessor = gltf->accessors[primitive.attributes.find("JOINTS_0")->second];
					const tinygltf::BufferView &jointView = gltf->bufferViews[jointAccessor.bufferView];
					bufferJoints = reinterpret_cast<const uint16_t*>(&(gltf->buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]));
				}

				// weights
				if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor & weightAccessor = gltf->accessors[primitive.attributes.find("WEIGHTS_0")->second];
					const tinygltf::BufferView & weightView = gltf->bufferViews[weightAccessor.bufferView];
					bufferWeights = reinterpret_cast<const float*>(&(gltf->buffers[weightView.buffer].data[weightAccessor.byteOffset + weightView.byteOffset]));
				}

				hasSkin = (bufferJoints && bufferWeights);

				for (size_t v = 0; v < posAccessor.count; v++) {
					Vertex vertex{};
					vertex.pos = glm::make_vec3(&bufferPos[v * 3]);
					vertex.normal = bufferNormals ? glm::normalize(glm::make_vec3(&bufferNormals[v * 3])) : glm::vec3(0.0f);
					vertex.texCoord = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
					vertex.joint0 = hasSkin ? glm::u16vec4(glm::make_vec4(&bufferJoints[v * 4])) : glm::u16vec4(0u);
					vertex.weight0 = hasSkin ? glm::make_vec4(&bufferWeights[v * 4]) : glm::vec4(0.0f);
					vertices.push_back(vertex);
				}
			}
			// indices
			{
				const tinygltf::Accessor &accessor = gltf->accessors[primitive.indices];
				const tinygltf::BufferView &bufferView = gltf->bufferViews[accessor.bufferView];
				const tinygltf::Buffer &buffer = gltf->buffers[bufferView.buffer];

				indexCount = static_cast<uint32_t>(accessor.count);
				const void *data = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					const uint32_t * buf = static_cast<const uint32_t*>(data);
					for (size_t i = 0; i < accessor.count; i++) {
						indices.push_back(buf[i] + vertexStart);
					}
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					const uint16_t * buf = static_cast<const uint16_t*>(data);
					for (size_t i = 0; i < accessor.count; i++) {
						indices.push_back(buf[i] + vertexStart);
					}
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					const uint8_t *buf = static_cast<const uint8_t*>(data);
					for (size_t i = 0; i < accessor.count; i++) {
						indices.push_back(buf[i] + vertexStart);
					}
					break;
				}
				default:
					log_error("index component type %d not supported!", accessor.componentType);
					assert(0);
				}
			}

			Primitive* newPrimitive = new Primitive(indexStart, indexCount);
			newMesh->primitives.push_back(newPrimitive);
		}
		newNode->mesh = newMesh;
	}
	if (parent) {
		parent->children.push_back(newNode);
	}
	/*else {
		nodes.push_back(newNode);
	}*/
	linearNodes.push_back(newNode);
}

void qb::Model::_loadSkins(){
	for (tinygltf::Skin& skin : gltf->skins) {
		Skin* newSkin = new Skin{};
		newSkin->name = skin.name;


		// find skeleton root node
		if (skin.skeleton > -1) {
			newSkin->skeletonRoot = _nodeFromIndex(skin.skeleton);
		}

		// find joint nodes
		for (int jointIndex : skin.joints) {
			qb::Model::Node* node = _nodeFromIndex(jointIndex);
			if (node != nullptr) {
				newSkin->joints.push_back(node);
			}
		}
		// get inverse bind mat
		if (skin.inverseBindMatrices > -1) {
			const tinygltf::Accessor& accessor = gltf->accessors[skin.inverseBindMatrices];
			const tinygltf::BufferView &bufferView = gltf->bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = gltf->buffers[bufferView.buffer];
			newSkin->inverseBindMat.resize(accessor.count);
			memcpy(newSkin->inverseBindMat.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
		}
		skins.push_back(newSkin);
	}
}

void qb::Model::_loadImages(){
	// todo
}

void qb::Model::_loadAnimations(){
	for (tinygltf::Animation &anim : gltf->animations) {
		qb::Model::Animation newAnimation{};
		newAnimation.name = anim.name;
		if (anim.name.empty()) {
			newAnimation.name = std::to_string(animations.size());
		}

		// samplers
		for (auto& samp : anim.samplers) {
			qb::Model::AnimationSampler newSampler{};
			if (samp.interpolation == "LINEAR") {
				newSampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
			}
			else if (samp.interpolation == "STEP") {
				newSampler.interpolation = AnimationSampler::InterpolationType::STEP;
			}
			else if (samp.interpolation == "CUBICSPLINE") {
				newSampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
			}

			// read sampler input time values
			{
				const tinygltf::Accessor&accessor = gltf->accessors[samp.input];
				const tinygltf::BufferView&bufferView = gltf->bufferViews[accessor.bufferView];
				const tinygltf::Buffer&buffer = gltf->buffers[bufferView.buffer];

				assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

				const void *data = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
				const float *buf = static_cast<const float *>(data);
				for (size_t i = 0; i < accessor.count; i++) {
					newSampler.inputs.push_back(buf[i]);
				}

				for (auto input : newSampler.inputs) {
					if (input < newAnimation.start) {
						newAnimation.start = input;
					}
					if (input > newAnimation.end) {
						newAnimation.end = input;
					}
				}
			}

			// read sampler output TRS value
			{
				const tinygltf::Accessor &accessor = gltf->accessors[samp.output];
				const tinygltf::BufferView &bufferView = gltf->bufferViews[accessor.bufferView];
				const tinygltf::Buffer &buffer = gltf->buffers[bufferView.buffer];

				assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

				const void *data = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
				switch (accessor.type) {
				case TINYGLTF_TYPE_VEC3: {
					const glm::vec3 *buf = static_cast<const glm::vec3*>(data);
					for (size_t i = 0; i < accessor.count; i++) {
						newSampler.outputsVec4.push_back(glm::vec4(buf[i], 1.0f));
					}
					break;
				}
				case TINYGLTF_TYPE_VEC4: {
					const glm::vec4 *buf = static_cast<const glm::vec4*>(data);
					for (size_t i = 0; i < accessor.count; i++) {
						newSampler.outputsVec4.push_back(buf[i]);
					}
					break;
				}
				default:
					log_error("unknown type: %d", accessor.type);
					assert(0);
				}
			}

			newAnimation.samplers.push_back(newSampler);
		}

		//channels
		for (auto& channel : anim.channels) {
			qb::Model::AnimationChannel newChannel{};

			if (channel.target_path == "rotation") {
				newChannel.path = AnimationChannel::ROTATION;
			}
			else if (channel.target_path == "translation") {
				newChannel.path = AnimationChannel::TRANSLATION;
			}
			else if (channel.target_path == "scale") {
				newChannel.path = AnimationChannel::SCALE;
			}
			if (channel.target_path == "weights") {
				log_warning("weights not yet supported, skipping channel");
				continue;
			}
			newChannel.samplerIndex = channel.sampler;
			newChannel.node = _nodeFromIndex(channel.target_node);
			if (!newChannel.node) {
				continue;
			}
			newAnimation.channels.push_back(newChannel);
		}
		animations.push_back(newAnimation);
	}
}

qb::Model::Node * qb::Model::_findNode(qb::Model::Node * parent, uint32_t index){

	qb::Model::Node* nodeFound = nullptr;
	if (parent->index == index) {
		return parent;
	}
	for (auto& child : parent->children) {
		nodeFound = _findNode(child, index);
		if (nodeFound != nullptr) {
			break;
		}
	}
	return nodeFound;
}

qb::Model::Node * qb::Model::_nodeFromIndex(uint32_t index){

	/*qb::Model::Node* nodeFound = nullptr;
	for (auto& node : nodes) {
		nodeFound = _findNode(node, index);
		if (nodeFound != nullptr) {
			break;
		}
	}
	return nodeFound;*/

	qb::Model::Node* nodeFound = nullptr;
	nodeFound = _findNode(rootNode, index);
	return nodeFound;
}

void qb::Model::setAnimation(std::string name){
	auto it = animMap.find(name);
	if (it == animMap.end()) {
		assert(0);
	}
	currAnimaiton = it->second;
}

void qb::Model::updateAnimation(float time){
	assert(currAnimaiton != nullptr);
	time = fmod(time, currAnimaiton->end - currAnimaiton->start);

	bool updated = false;
	for (auto& channel : currAnimaiton->channels) {
		qb::Model::AnimationSampler &sampler = currAnimaiton->samplers[channel.samplerIndex];
		if (sampler.inputs.size() > sampler.outputsVec4.size()) {
			continue;
		}

		for (size_t i = 0; i < sampler.inputs.size() - 1; i++) {
			if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
				float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);

				if (u <= 1.0f) {
					switch (channel.path) {
					case qb::Model::AnimationChannel::PathType::TRANSLATION: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->translation = glm::vec3(trans);
						break;
					}
					case qb::Model::AnimationChannel::PathType::SCALE: {
						glm::vec4 scale = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->scale = glm::vec3(scale);
						break;
					}
					case qb::Model::AnimationChannel::PathType::ROTATION: {
						glm::quat q1;
						q1.x = sampler.outputsVec4[i].x;
						q1.y = sampler.outputsVec4[i].y;
						q1.z = sampler.outputsVec4[i].z;
						q1.w = sampler.outputsVec4[i].w;
						glm::quat q2;
						q2.x = sampler.outputsVec4[i + 1].x;
						q2.y = sampler.outputsVec4[i + 1].y;
						q2.z = sampler.outputsVec4[i + 1].z;
						q2.w = sampler.outputsVec4[i + 1].w;
						channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));
						break;
					}
					}
					updated = true;
				}
			}
		}
	}
	if (updated) {
		rootNode->update();
		/*for (auto&node : nodes) {
			node->update();
		}*/
	}
}

void qb::Model::init(App * app, std::string name){
	this->app = app;
	this->name = name;
}

void qb::Model::destroy(){
	for (auto& node : linearNodes) {

		auto& mesh = node->mesh;
		if (mesh != nullptr) {
			for (auto& primitive : mesh->primitives) {
				delete primitive;
			}
		}
		delete mesh;

		auto& skin = node->skin;
		if (skin != nullptr) {
			delete node->skin;
		}

		delete node;
	}
}

void qb::Model::build(){
	assert(gltf != nullptr);
	_loadImages();
	//todo load material

	rootNode = new Node();
	rootNode->name = "$root";
	rootNode->index = -1;
	rootNode->translation = glm::vec3(0.0f);
	rootNode->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	rootNode->scale = glm::vec3(1.0f);
	rootNode->mat = glm::mat4(1.0f);
	linearNodes.push_back(rootNode);

	const tinygltf::Scene &scene = gltf->scenes[gltf->defaultScene > -1 ? gltf->defaultScene : 0];
	for (size_t i = 0; i < scene.nodes.size(); i++) {
		const tinygltf::Node node = gltf->nodes[scene.nodes[i]];
		_loadNode(rootNode, node, scene.nodes[i]);
	}
	if (gltf->animations.size() > 0) {
		_loadAnimations();
	}
	_loadSkins();

	// node ref skin
	for (auto& node : linearNodes) {
		if (node->skinIndex > -1) {
			node->skin = skins[node->skinIndex];
		}
	}

	// anim map
	for (auto& anim : animations) {
		animMap[anim.name] = &anim;
	}

	// uniform buf
	for (auto& node : linearNodes) {
		if (node->mesh) {
			// uniform buffer
			std::string uniqueName = name + "/" + node->name + "/" + node->mesh->name;
			std::string uniBufName = "$mesh_" + uniqueName;
			Buffer* uniBuf = app->bufferMgr.getBuffer(uniBufName);
			uniBuf->bufferInfo.size = sizeof(qb::Model::Mesh::Uniform);
			uniBuf->bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			uniBuf->descriptorRange = sizeof(qb::Model::Mesh::Uniform);
			uniBuf->buildPerSwapchainImg();
			node->mesh->uniBuf = uniBuf;
			// descriptor set
			std::string descriptorName = "$descriptor_" + uniqueName;
			Descriptor* descriptor = app->descriptorMgr.getDescriptor(descriptorName);
			descriptor->bindings = {
				{descriptor_layout_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT), uniBuf}
			};
			descriptor->buildPerSwapchainImg();
			node->mesh->descriptor = descriptor;
			linearMeshes.push_back(node->mesh);

			// update mesh
			if (node->mesh) {
				node->update();
			}
		}
	}
	
	// vertex buf
	vertexBuf = app->bufferMgr.getBuffer("$vertex_" + name);
	vertexBuf->bufferInfo.size = sizeof(qb::Model::Vertex) * vertices.size();
	vertexBuf->bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vertexBuf->build();
	vertexBuf->mapping(vertices.data());
	
	// index buf
	indexBuf = app->bufferMgr.getBuffer("$index_" + name);
	indexBuf->bufferInfo.size = sizeof(uint32_t) * indices.size();
	indexBuf->bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	indexBuf->build();
	indexBuf->mapping(indices.data());

}

glm::mat4 qb::Model::Node::localMat() {
	return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * mat;
}

glm::mat4 qb::Model::Node::globalMat() {
	// todo: need optimize!, use dirty flag in parent
	glm::mat4 m = localMat();
	Node *p = parent;
	while (p) {
		m = p->localMat() * m;
		p = p->parent;
	}
	return m;
}

void qb::Model::Node::update() {
	if (mesh) {
		glm::mat4 m = globalMat();
		mesh->uniform.mat = m;
		if (skin) {
			glm::mat4 inverseTransform = glm::inverse(m);
			size_t numJoints = std::min((uint32_t)skin->joints.size(), (uint32_t)max_bones_per_mesh);
			for (size_t i = 0; i < numJoints; i++) {
				Node* jointNode = skin->joints[i];
				glm::mat4 jointMat = jointNode->globalMat() * skin->inverseBindMat[i];
				jointMat = inverseTransform * jointMat;
				mesh->uniform.joinMat[i] = jointMat;
			}
			mesh->uniBuf->mappingCurSwapchainImg(&mesh->uniform);
		}
		else {
			mesh->uniBuf->mappingCurSwapchainImg(&mesh->uniform.mat, offsetof(qb::Model::Mesh::Uniform, mat), sizeof(mesh->uniform.mat));
		}
	}
	for (auto& child : children) {
		child->update();
	}
}
