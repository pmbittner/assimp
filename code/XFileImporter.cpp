/*
---------------------------------------------------------------------------
Open Asset Import Library (ASSIMP)
---------------------------------------------------------------------------

Copyright (c) 2006-2008, ASSIMP Development Team

All rights reserved.

Redistribution and use of this software in source and binary forms, 
with or without modification, are permitted provided that the following 
conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the ASSIMP team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the ASSIMP Development Team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/
/** @file Implementation of the XFile importer class */
#include "XFileImporter.h"
#include "XFileParser.h"
#include "MaterialSystem.h"
#include "ConvertToLHProcess.h"

#include "../include/IOStream.h"
#include "../include/IOSystem.h"
#include "../include/aiMesh.h"
#include "../include/aiScene.h"

#include <boost/scoped_ptr.hpp>
#include <boost/format.hpp>

using namespace Assimp;

// ------------------------------------------------------------------------------------------------
// Constructor to be privately used by Importer
XFileImporter::XFileImporter()
{
}

// ------------------------------------------------------------------------------------------------
// Destructor, private as well 
XFileImporter::~XFileImporter()
{
}

// ------------------------------------------------------------------------------------------------
// Returns whether the class can handle the format of the given file. 
bool XFileImporter::CanRead( const std::string& pFile, IOSystem* pIOHandler) const
{
	// simple check of file extension is enough for the moment
	std::string::size_type pos = pFile.find_last_of( '.');
	// no file extension - can't read
	if( pos == std::string::npos)
		return false;
	std::string extension = pFile.substr( pos);
	if( extension == ".x" || extension == ".X")
		return true;

	return false;
}

// ------------------------------------------------------------------------------------------------
// Imports the given file into the given scene structure. 
void XFileImporter::InternReadFile( const std::string& pFile, aiScene* pScene, IOSystem* pIOHandler)
{
	// read file into memory
	boost::scoped_ptr<IOStream> file( pIOHandler->Open( pFile));
	if( file.get() == NULL)
		throw new ImportErrorException( "Failed to open file " + pFile + ".");

	size_t fileSize = file->FileSize();
	if( fileSize < 16)
		throw new ImportErrorException( "XFile is too small.");

	mBuffer.resize( fileSize);
	file->Read( &mBuffer.front(), 1, fileSize);

	// parse the file into a temporary representation
	XFileParser parser( mBuffer);

	// and create the proper return structures out of it
	CreateDataRepresentationFromImport( pScene, parser.GetImportedData());

  // if nothing came from it, report it as error
  if( !pScene->mRootNode)
    throw new ImportErrorException( "XFile is ill-formatted - no content imported.");
}

// ------------------------------------------------------------------------------------------------
// Constructs the return data structure out of the imported data.
void XFileImporter::CreateDataRepresentationFromImport( aiScene* pScene, const XFile::Scene* pData)
{
	// Read the global materials first so that meshes referring to them can find them later
	ConvertMaterials( pScene, pData->mGlobalMaterials);

	// copy nodes, extracting meshes and materials on the way
	pScene->mRootNode = CreateNodes( pScene, NULL, pData->mRootNode);

	// extract animations
	CreateAnimations( pScene, pData);

	// read the global meshes that were stored outside of any node
	if( pData->mGlobalMeshes.size() > 0)
	{
		// create a root node to hold them if there isn't any, yet
		if( pScene->mRootNode == NULL)
		{
			pScene->mRootNode = new aiNode;
			pScene->mRootNode->mName.Set( "$dummy_node");
		}

		// convert all global meshes and store them in the root node.
		// If there was one before, the global meshes now suddenly have its transformation matrix...
		// Don't know what to do there, I don't want to insert another node under the present root node
		// just to avoid this.
		CreateMeshes( pScene, pScene->mRootNode, pData->mGlobalMeshes);
	}

	// convert the root node's transformation to OGL coords
  if( pScene->mRootNode)
  	ConvertToLHProcess::ConvertToOGL( pScene->mRootNode->mTransformation);

	// finally: create a dummy material if not material was imported
	if( pScene->mNumMaterials == 0)
	{
		pScene->mNumMaterials = 1;
		// create the Material
		Assimp::MaterialHelper* mat = new Assimp::MaterialHelper;
		int shadeMode = (int) aiShadingMode_Gouraud;
		mat->AddProperty<int>( &shadeMode, 1, AI_MATKEY_SHADING_MODEL);
		// material colours
		int specExp = 1;
		mat->AddProperty( &aiColor3D( 0, 0, 0), 1, AI_MATKEY_COLOR_EMISSIVE);
		mat->AddProperty( &aiColor3D( 0.5f, 0.5f, 0.5f), 1, AI_MATKEY_COLOR_DIFFUSE);
		mat->AddProperty( &aiColor3D( 0, 0, 0), 1, AI_MATKEY_COLOR_SPECULAR);
		mat->AddProperty( &specExp, 1, AI_MATKEY_SHININESS);

		pScene->mMaterials = new aiMaterial*[1];
		pScene->mMaterials[0] = mat;
	}
}

// ------------------------------------------------------------------------------------------------
// Recursively creates scene nodes from the imported hierarchy. 
aiNode* XFileImporter::CreateNodes( aiScene* pScene, aiNode* pParent, const XFile::Node* pNode)
{
	if( !pNode)
		return NULL;

	// create node
	aiNode* node = new aiNode;
	node->mName.length = pNode->mName.length();
	node->mParent = pParent;
	memcpy( node->mName.data, pNode->mName.c_str(), pNode->mName.length());
	node->mName.data[node->mName.length] = 0;
	node->mTransformation = pNode->mTrafoMatrix;
	
	// convert meshes from the source node 
	CreateMeshes( pScene, node, pNode->mMeshes);

	// handle childs
	if( pNode->mChildren.size() > 0)
	{
		node->mNumChildren = (unsigned int)pNode->mChildren.size();
		node->mChildren = new aiNode* [node->mNumChildren];

		for( unsigned int a = 0; a < pNode->mChildren.size(); a++)
			node->mChildren[a] = CreateNodes( pScene, node, pNode->mChildren[a]);
	}

	return node;
}

// ------------------------------------------------------------------------------------------------
// Creates the meshes for the given node. 
void XFileImporter::CreateMeshes( aiScene* pScene, aiNode* pNode, const std::vector<XFile::Mesh*>& pMeshes)
{
	if( pMeshes.size() == 0)
		return;

	// create a mesh for each mesh-material combination in the source node
	std::vector<aiMesh*> meshes;
	for( unsigned int a = 0; a < pMeshes.size(); a++)
	{
		const XFile::Mesh* sourceMesh = pMeshes[a];
		// first convert its materials so that we can find them when searching by name afterwards
		ConvertMaterials( pScene, sourceMesh->mMaterials);

		unsigned int numMaterials = std::max( (unsigned int)sourceMesh->mMaterials.size(), 1u);
		for( unsigned int b = 0; b < numMaterials; b++)
		{
			// collect the faces belonging to this material
			std::vector<unsigned int> faces;
			unsigned int numVertices = 0;
			if( sourceMesh->mFaceMaterials.size() > 0)
			{
				// if there is a per-face material defined, select the faces with the corresponding material
				for( unsigned int c = 0; c < sourceMesh->mFaceMaterials.size(); c++)
				{
					if( sourceMesh->mFaceMaterials[c] == b)
					{
						faces.push_back( c);
						numVertices += (unsigned int)sourceMesh->mPosFaces[c].mIndices.size();
					}
				}
			} else
			{
				// if there is no per-face material, place everything into one mesh
				for( unsigned int c = 0; c < sourceMesh->mPosFaces.size(); c++)
				{
					faces.push_back( c);
					numVertices += (unsigned int)sourceMesh->mPosFaces[c].mIndices.size();
				}
			}

			// no faces/vertices using this material? strange...
			if( numVertices == 0)
				continue;

			// create a submesh using this material
			aiMesh* mesh = new aiMesh;
			meshes.push_back( mesh);

			// find the material by name in the scene's material list. Either own material
			// or referenced material, it should already be found there
			if( sourceMesh->mFaceMaterials.size() > 0)
			{
				std::map<std::string, unsigned int>::const_iterator matIt = mImportedMats.find( sourceMesh->mMaterials[b].mName);
				if( matIt == mImportedMats.end())
					mesh->mMaterialIndex = 0;
				else
					mesh->mMaterialIndex = matIt->second;
			} else
			{
				mesh->mMaterialIndex = 0;
			}

			// Create properly sized data arrays in the mesh. We store unique vertices per face,
			// as specified 
			mesh->mNumVertices = numVertices;
			mesh->mVertices = new aiVector3D[numVertices];
			mesh->mNumFaces = (unsigned int)faces.size();
			mesh->mFaces = new aiFace[mesh->mNumFaces];

			// normals?
			if( sourceMesh->mNormals.size() > 0)
				mesh->mNormals = new aiVector3D[numVertices];
			// texture coords
			for( unsigned int c = 0; c < AI_MAX_NUMBER_OF_TEXTURECOORDS; c++)
			{
				if( sourceMesh->mTexCoords[c].size() > 0)
					mesh->mTextureCoords[c] = new aiVector3D[numVertices];
			}
			// vertex colors
			for( unsigned int c = 0; c < AI_MAX_NUMBER_OF_COLOR_SETS; c++)
			{
				if( sourceMesh->mColors[c].size() > 0)
					mesh->mColors[c] = new aiColor4D[numVertices];
			}

			// now collect the vertex data of all data streams present in the imported mesh
			unsigned int newIndex = 0;
			std::vector<unsigned int> orgPoints; // from which original point each new vertex stems
			orgPoints.resize( numVertices, 0);

			for( unsigned int c = 0; c < faces.size(); c++)
			{
				unsigned int f = faces[c]; // index of the source face
				const XFile::Face& pf = sourceMesh->mPosFaces[f]; // position source face

				// create face. either triangle or triangle fan depending on the index count
				aiFace& df = mesh->mFaces[c]; // destination face
				df.mNumIndices = (unsigned int)pf.mIndices.size();
				df.mIndices = new unsigned int[ df.mNumIndices];

				// collect vertex data for indices of this face
				for( unsigned int d = 0; d < df.mNumIndices; d++)
				{
					df.mIndices[df.mNumIndices - 1 - d] = newIndex; // inverted face orientation for OGL
					orgPoints[newIndex] = pf.mIndices[d];

					// Position
					mesh->mVertices[newIndex] = sourceMesh->mPositions[pf.mIndices[d]];
					// Normal, if present
					if( mesh->HasNormals())
						mesh->mNormals[newIndex] = sourceMesh->mNormals[sourceMesh->mNormFaces[f].mIndices[d]];
					// texture coord sets
					for( unsigned int e = 0; e < AI_MAX_NUMBER_OF_TEXTURECOORDS; e++)
					{
						if( mesh->HasTextureCoords( e))
						{
							aiVector2D tex = sourceMesh->mTexCoords[e][pf.mIndices[d]];
							mesh->mTextureCoords[e][newIndex] = aiVector3D( tex.x, 1.0f - tex.y, 0.0f);
						}
					}
					// vertex color sets
					for( unsigned int e = 0; e < AI_MAX_NUMBER_OF_COLOR_SETS; e++)
						if( mesh->HasVertexColors( e))
							mesh->mColors[e][newIndex] = sourceMesh->mColors[e][pf.mIndices[d]];
					
					newIndex++;
				}
			}

			// there should be as much new vertices as we calculated before
			assert( newIndex == numVertices);

			// convert all bones of the source mesh which influence vertices in this newly created mesh
			const std::vector<XFile::Bone>& bones = sourceMesh->mBones;
			std::vector<aiBone*> newBones;
			for( unsigned int c = 0; c < bones.size(); c++)
			{
				const XFile::Bone& obone = bones[c];
				// set up a vertex-linear array of the weights for quick searching if a bone influences a vertex
				std::vector<float> oldWeights( sourceMesh->mPositions.size(), 0.0f);
				for( unsigned int d = 0; d < obone.mWeights.size(); d++)
					oldWeights[obone.mWeights[d].mVertex] = obone.mWeights[d].mWeight;

				// collect all vertex weights that influence a vertex in the new mesh
				std::vector<aiVertexWeight> newWeights;
				newWeights.reserve( numVertices);
				for( unsigned int d = 0; d < orgPoints.size(); d++)
				{
					// does the new vertex stem from an old vertex which was influenced by this bone?
					float w = oldWeights[orgPoints[d]];
					if( w > 0.0f)
						newWeights.push_back( aiVertexWeight( d, w));
				}

				// if the bone has no weights in the newly created mesh, ignore it
				if( newWeights.size() == 0)
					continue;

				// create
				aiBone* nbone = new aiBone;
				newBones.push_back( nbone);
				// copy name and matrix
				nbone->mName.Set( obone.mName);
				nbone->mOffsetMatrix = obone.mOffsetMatrix;
				nbone->mNumWeights = (unsigned int)newWeights.size();
				nbone->mWeights = new aiVertexWeight[nbone->mNumWeights];
				for( unsigned int d = 0; d < newWeights.size(); d++)
					nbone->mWeights[d] = newWeights[d];
			}

			// store the bones in the mesh
			mesh->mNumBones = (unsigned int)newBones.size();
			mesh->mBones = new aiBone*[mesh->mNumBones];
			for( unsigned int c = 0; c < newBones.size(); c++)
				mesh->mBones[c] = newBones[c];
		}
	}

	// reallocate scene mesh array to be large enough
	aiMesh** prevArray = pScene->mMeshes;
	pScene->mMeshes = new aiMesh*[pScene->mNumMeshes + meshes.size()];
	if( prevArray)
	{
		memcpy( pScene->mMeshes, prevArray, pScene->mNumMeshes * sizeof( aiMesh*));
		delete [] prevArray;
	}

	// allocate mesh index array in the node
	pNode->mNumMeshes = (unsigned int)meshes.size();
	pNode->mMeshes = new unsigned int[pNode->mNumMeshes];

	// store all meshes in the mesh library of the scene and store their indices in the node
	for( unsigned int a = 0; a < meshes.size(); a++)
	{
		pScene->mMeshes[pScene->mNumMeshes] = meshes[a];		
		pNode->mMeshes[a] = pScene->mNumMeshes;
		pScene->mNumMeshes++;
	}
}

// ------------------------------------------------------------------------------------------------
// Converts the animations from the given imported data and creates them in the scene.
void XFileImporter::CreateAnimations( aiScene* pScene, const XFile::Scene* pData)
{
	std::vector<aiAnimation*> newAnims;

	for( unsigned int a = 0; a < pData->mAnims.size(); a++)
	{
		const XFile::Animation* anim = pData->mAnims[a];
		// create a new animation to hold the data
		aiAnimation* nanim = new aiAnimation;
		newAnims.push_back( nanim);
		nanim->mName.Set( anim->mName);
		// duration will be determined by the maximum length
		nanim->mDuration = 0;
		nanim->mTicksPerSecond = pData->mAnimTicksPerSecond;
		nanim->mNumBones = (unsigned int)anim->mAnims.size();
		nanim->mBones = new aiBoneAnim*[nanim->mNumBones];

		for( unsigned int b = 0; b < anim->mAnims.size(); b++)
		{
			const XFile::AnimBone* bone = anim->mAnims[b];
			aiBoneAnim* nbone = new aiBoneAnim;
			nbone->mBoneName.Set( bone->mBoneName);
			nanim->mBones[b] = nbone;

			// apply the LH->RH conversion if the animation affects the root bone
			bool isRootAnim = (bone->mBoneName == pScene->mRootNode->mName.data);

			// keyframes are given as combined transformation matrix keys
			if( bone->mTrafoKeys.size() > 0)
			{
				nbone->mNumPositionKeys = (unsigned int)bone->mTrafoKeys.size();
				nbone->mPositionKeys = new aiVectorKey[nbone->mNumPositionKeys];
				nbone->mNumRotationKeys = (unsigned int)bone->mTrafoKeys.size();
				nbone->mRotationKeys = new aiQuatKey[nbone->mNumRotationKeys];
				nbone->mNumScalingKeys = (unsigned int)bone->mTrafoKeys.size();
				nbone->mScalingKeys = new aiVectorKey[nbone->mNumScalingKeys];

				for( unsigned int c = 0; c < bone->mTrafoKeys.size(); c++)
				{
					// deconstruct each matrix into separate position, rotation and scaling
					double time = bone->mTrafoKeys[c].mTime;
					aiMatrix4x4 trafo = bone->mTrafoKeys[c].mMatrix;

					// extract position
					aiVector3D pos( trafo.a4, trafo.b4, trafo.c4);
					if( isRootAnim)
						ConvertToLHProcess::ConvertToOGL( pos);

					nbone->mPositionKeys[c].mTime = time;
					nbone->mPositionKeys[c].mValue = pos;

					// extract scaling
					aiVector3D scale;
					scale.x = aiVector3D( trafo.a1, trafo.b1, trafo.c1).Length();
					scale.y = aiVector3D( trafo.a2, trafo.b2, trafo.c2).Length();
					scale.z = aiVector3D( trafo.a3, trafo.b3, trafo.c3).Length();
					nbone->mScalingKeys[c].mTime = time;
					nbone->mScalingKeys[c].mValue = scale;

					// reconstruct rotation matrix without scaling
					aiMatrix3x3 rotmat( 
						trafo.a1 / scale.x, trafo.a2 / scale.y, trafo.a3 / scale.z,
						trafo.b1 / scale.x, trafo.b2 / scale.y, trafo.b3 / scale.z,
						trafo.c1 / scale.x, trafo.c2 / scale.y, trafo.c3 / scale.z);

					if( isRootAnim)
						ConvertToLHProcess::ConvertToOGL( rotmat);

					// and convert it into a quaternion
					nbone->mRotationKeys[c].mTime = time;
					nbone->mRotationKeys[c].mValue = aiQuaternion( rotmat);
				}

				// longest lasting key sequence determines duration
				nanim->mDuration = std::max( nanim->mDuration, bone->mTrafoKeys.back().mTime);
			} else
			{
				// separate key sequences for position, rotation, scaling
				nbone->mNumPositionKeys = (unsigned int)bone->mPosKeys.size(); 
				nbone->mPositionKeys = new aiVectorKey[nbone->mNumPositionKeys];
				for( unsigned int c = 0; c < nbone->mNumPositionKeys; c++)
				{
					aiVector3D pos = bone->mPosKeys[c].mValue;
					if( isRootAnim)
						ConvertToLHProcess::ConvertToOGL( pos);

					nbone->mPositionKeys[c].mTime = bone->mPosKeys[c].mTime;
					nbone->mPositionKeys[c].mValue = pos;
				}

				// rotation
				nbone->mNumRotationKeys = (unsigned int)bone->mRotKeys.size(); 
				nbone->mRotationKeys = new aiQuatKey[nbone->mNumRotationKeys];
				for( unsigned int c = 0; c < nbone->mNumRotationKeys; c++)
				{
					aiMatrix3x3 rotmat = bone->mRotKeys[c].mValue.GetMatrix();
					if( isRootAnim)
						ConvertToLHProcess::ConvertToOGL( rotmat);

					nbone->mRotationKeys[c].mTime = bone->mRotKeys[c].mTime;
					nbone->mRotationKeys[c].mValue = aiQuaternion( rotmat);
				}

				// scaling
				nbone->mNumScalingKeys = (unsigned int)bone->mScaleKeys.size(); 
				nbone->mScalingKeys = new aiVectorKey[nbone->mNumScalingKeys];
				for( unsigned int c = 0; c < nbone->mNumScalingKeys; c++)
					nbone->mScalingKeys[c] = bone->mScaleKeys[c];

				// longest lasting key sequence determines duration
				if( bone->mPosKeys.size() > 0)
					nanim->mDuration = std::max( nanim->mDuration, bone->mPosKeys.back().mTime);
				if( bone->mRotKeys.size() > 0)
					nanim->mDuration = std::max( nanim->mDuration, bone->mRotKeys.back().mTime);
				if( bone->mScaleKeys.size() > 0)
					nanim->mDuration = std::max( nanim->mDuration, bone->mScaleKeys.back().mTime);
			}
		}
	}

	// store all converted animations in the scene
	if( newAnims.size() > 0)
	{
		pScene->mNumAnimations = (unsigned int)newAnims.size();
		pScene->mAnimations = new aiAnimation* [pScene->mNumAnimations];
		for( unsigned int a = 0; a < newAnims.size(); a++)
			pScene->mAnimations[a] = newAnims[a];
	}
}

// ------------------------------------------------------------------------------------------------
// Converts all materials in the given array and stores them in the scene's material list.
void XFileImporter::ConvertMaterials( aiScene* pScene, const std::vector<XFile::Material>& pMaterials)
{
	// count the non-referrer materials in the array
	unsigned int numMaterials = 0;
	for( unsigned int a = 0; a < pMaterials.size(); a++)
		if( !pMaterials[a].mIsReference)
			numMaterials++;

	if( numMaterials == 0)
		return;

	// resize the scene's material list to offer enough space for the new materials
	aiMaterial** prevMats = pScene->mMaterials;
	pScene->mMaterials = new aiMaterial*[pScene->mNumMaterials + numMaterials];
	if( prevMats)
	{
		memcpy( pScene->mMaterials, prevMats, pScene->mNumMaterials * sizeof( aiMaterial*));
		delete [] prevMats;
	}

	// convert all the materials given in the array
	for( unsigned int a = 0; a < pMaterials.size(); a++)
	{
		const XFile::Material& oldMat = pMaterials[a];
		if( oldMat.mIsReference)
			continue;

		Assimp::MaterialHelper* mat = new Assimp::MaterialHelper;
		aiString name;
		name.Set( oldMat.mName);
		mat->AddProperty( &name, AI_MATKEY_NAME);

		// Shading model: hardcoded to PHONG, there is no such information in an XFile
		// FIX (aramis): If the specular exponent is 0, use gouraud shading. This is a bugfix
		// for some models in the SDK (e.g. good old tiny.x)
		int shadeMode = (int)oldMat.mSpecularExponent == 0.0f 
			? aiShadingMode_Gouraud : aiShadingMode_Phong;

		mat->AddProperty<int>( &shadeMode, 1, AI_MATKEY_SHADING_MODEL);
		// material colours
		// FIX: Setup this as ambient not as emissive color
		mat->AddProperty( &oldMat.mEmissive, 1, AI_MATKEY_COLOR_AMBIENT);
		mat->AddProperty( &oldMat.mDiffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
		mat->AddProperty( &oldMat.mSpecular, 1, AI_MATKEY_COLOR_SPECULAR);
		mat->AddProperty( &oldMat.mSpecularExponent, 1, AI_MATKEY_SHININESS);


		// texture, if there is one
		if (1 == oldMat.mTextures.size())
			{
			// if there is only one texture, assume it contains the
			// diffuse color
			aiString tex;
			tex.Set( oldMat.mTextures[0]);

			mat->AddProperty( &tex, AI_MATKEY_TEXTURE_DIFFUSE(0));
			}
		else
			{
			// Otherwise ... try to search for typical strings in the
			// texture's file name like 'bump' or 'diffuse'
			unsigned int iHM = 0,iNM = 0,iDM = 0,iSM = 0,iAM = 0,iEM = 0;
			for( unsigned int b = 0; b < oldMat.mTextures.size(); b++)
				{
				std::string sz = oldMat.mTextures[b];
				char key[256];

				// find the file name
				const size_t iLen = sz.length();
				std::string::size_type s = sz.rfind('\\',iLen-1);
				if (std::string::npos == s)
					{
					s = sz.rfind('/',iLen-1);
					if (std::string::npos == s)s = 0;
					}

				// cut off the file extension
				std::string::size_type sExt = sz.rfind('.',iLen-1);
				if (std::string::npos != sExt)
					{
					sz[sExt] = '\0';
					}

				// bump map
				std::string::size_type s2 = sz.find("bump",s);
				if (std::string::npos == s2)
					{
					s2 = sz.find("BUMP",s);
					if (std::string::npos == s2)
						{
						s2 = sz.find("Bump",s);
						if (std::string::npos == s2)
							{
							s2 = sz.find("height",s);
							if (std::string::npos == s2)
								{
								s2 = sz.find("HEIGHT",s);
								if (std::string::npos == s2)
									{
									s2 = sz.find("Height",s);
									}
								}
							}
						}
					}
				if (std::string::npos != s2)
					{
					sprintf(key,AI_MATKEY_TEXTURE_HEIGHT_ "[%i]",iHM++);
					}
				else
					{
					// Normal map
					std::string::size_type s2 = sz.find("normal",s);
					if (std::string::npos == s2)
						{
						s2 = sz.find("NORMAL",s);
						if (std::string::npos == s2)
							{
							s2 = sz.find("nm",s); // not really unique
							if (std::string::npos == s2)
								{
								s2 = sz.find("Normal",s); 
								if (std::string::npos == s2)
									{
									s2 = sz.find("NM",s); 
									}
								}
							}
						}
					if (std::string::npos != s2)
						{
						sprintf(key,AI_MATKEY_TEXTURE_NORMALS_ "[%i]",iNM++);
						}
					else
						{

						// specular color texture (not unique, too. Could
						// also be the material's shininess)
						std::string::size_type s2 = sz.find("spec",s);
						if (std::string::npos == s2)
							{
							s2 = sz.find("Spec",s);
							if (std::string::npos == s2)
								{
								s2 = sz.find("SPEC",s);
								if (std::string::npos == s2)
									{
									s2 = sz.find("Glanz",s);
									if (std::string::npos == s2)
										{
										s2 = sz.find("glanz",s);
										}
									}
								}
							}
						if (std::string::npos != s2)
							{
							sprintf(key,AI_MATKEY_TEXTURE_SPECULAR_ "[%i]",iSM++);
							}
						else
							{
							// ambient color texture
							std::string::size_type s2 = sz.find("ambi",s);
							if (std::string::npos == s2)
								{
								s2 = sz.find("AMBI",s);
								if (std::string::npos == s2)
									{
									s2 = sz.find("umgebungsfarbe",s);
									if (std::string::npos == s2)
										{
										s2 = sz.find("Ambi",s);
										}
									}
								}
							if (std::string::npos != s2)
								{
								sprintf(key,AI_MATKEY_TEXTURE_AMBIENT_ "[%i]",iAM++);
								}
							else
								{
								// emissive color texture
								std::string::size_type s2 = sz.find("emissive",s);
								if (std::string::npos == s2)
									{
									s2 = sz.find("EMISSIVE",s);
									if (std::string::npos == s2)
										{
										// self illumination
										s2 = sz.find("self",s);
										if (std::string::npos == s2)
											{
											s2 = sz.find("Emissive",s);
											}
										}
									}
								if (std::string::npos != s2)
									{
									sprintf(key,AI_MATKEY_TEXTURE_EMISSIVE_ "[%i]",iEM++);
									}
								else
									{
									// assume it is a diffuse texture
									sprintf(key,AI_MATKEY_TEXTURE_DIFFUSE_ "[%i]",iDM++);
									}
								}
							}
						}
					}

				aiString tex;
				tex.Set( oldMat.mTextures[b] );

				mat->AddProperty( &tex, key);
				}

			}
		pScene->mMaterials[pScene->mNumMaterials] = mat;
		mImportedMats[oldMat.mName] = pScene->mNumMaterials;
		pScene->mNumMaterials++;
	}
}
