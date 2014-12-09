#include "terrain.h"
#include "camera.h"

const DWORD TERRAINVertex::FVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2;

//////////////////////////////////////////////////////////////////////////////////////////
//									PATCH												//
//////////////////////////////////////////////////////////////////////////////////////////

PATCH::PATCH()
{
	m_pDevice = NULL;
	m_pMesh = NULL;
}
PATCH::~PATCH()
{
	Release();
}

void PATCH::Release()
{
	if(m_pMesh != NULL)
		m_pMesh->Release();
	m_pMesh = NULL;
}

HRESULT PATCH::CreateMesh(TERRAIN &ter, RECT source, IDirect3DDevice9* Dev)
{
	if(m_pMesh != NULL)
	{
		m_pMesh->Release();
		m_pMesh = NULL;
	}

	try
	{
		m_pDevice = Dev;
		m_mapRect = source;

		int width = source.right - source.left;
		int height = source.bottom - source.top;
		int nrVert = (width + 1) * (height + 1);
		int nrTri = width * height * 2;

		if(FAILED(D3DXCreateMeshFVF(nrTri, nrVert, D3DXMESH_MANAGED, TERRAINVertex::FVF, m_pDevice, &m_pMesh)))
		{
			debug.Print("Couldn't create mesh for PATCH");
			return E_FAIL;
		}

		m_BBox.max = D3DXVECTOR3(-10000.0f, -10000.0f, -10000.0f);
		m_BBox.min = D3DXVECTOR3(10000.0f, 10000.0f, 10000.0f);

		//Create vertices
		TERRAINVertex* ver = 0;
		m_pMesh->LockVertexBuffer(0,(void**)&ver);
		for(int z=source.top, z0 = 0;z<=source.bottom;z++, z0++)
			for(int x=source.left, x0 = 0;x<=source.right;x++, x0++)
			{
				MAPTILE *tile = ter.GetTile(x, z);

				D3DXVECTOR3 pos = D3DXVECTOR3((float)x, tile->m_height, (float)-z);
				D3DXVECTOR2 alphaUV = D3DXVECTOR2(x / (float)ter.m_size.x, z / (float)ter.m_size.y);		//Alpha UV
				D3DXVECTOR2 colorUV = alphaUV * 8.0f;													//Color UV
				ver[z0 * (width + 1) + x0] = TERRAINVertex(pos, alphaUV, colorUV);

				//Calculate bounding box bounds...
				if(pos.x < m_BBox.min.x)m_BBox.min.x = pos.x;
				if(pos.x > m_BBox.max.x)m_BBox.max.x = pos.x;
				if(pos.y < m_BBox.min.y)m_BBox.min.y = pos.y;
				if(pos.y > m_BBox.max.y)m_BBox.max.y = pos.y;
				if(pos.z < m_BBox.min.z)m_BBox.min.z = pos.z;
				if(pos.z > m_BBox.max.z)m_BBox.max.z = pos.z;
			}
		m_pMesh->UnlockVertexBuffer();

		//Calculate Indices
		WORD* ind = 0;
		m_pMesh->LockIndexBuffer(0,(void**)&ind);	
		int index = 0;

		for(int z=source.top, z0 = 0;z<source.bottom;z++, z0++)
			for(int x=source.left, x0 = 0;x<source.right;x++, x0++)
			{
				//Triangle 1
				ind[index++] =   z0   * (width + 1) + x0;
				ind[index++] =   z0   * (width + 1) + x0 + 1;
				ind[index++] = (z0+1) * (width + 1) + x0;		

				//Triangle 2
				ind[index++] = (z0+1) * (width + 1) + x0;
				ind[index++] =   z0   * (width + 1) + x0 + 1;
				ind[index++] = (z0+1) * (width + 1) + x0 + 1;
			}

		m_pMesh->UnlockIndexBuffer();

		//Set Attributes
		DWORD *att = 0, a = 0;
		m_pMesh->LockAttributeBuffer(0,&att);
		memset(att, 0, sizeof(DWORD)*nrTri);
		m_pMesh->UnlockAttributeBuffer();

		//Compute normals
		D3DXComputeNormals(m_pMesh, NULL);
	}
	catch(...)
	{
		debug.Print("Error in PATCH::CreateMesh()");
		return E_FAIL;
	}

	return S_OK;
}

void PATCH::Render()
{
	//Draw mesh
	if(m_pMesh != NULL)
		m_pMesh->DrawSubset(0);
}

//////////////////////////////////////////////////////////////////////////////////////////
//									TERRAIN												//
//////////////////////////////////////////////////////////////////////////////////////////

TERRAIN::TERRAIN()
{
	m_pDevice = NULL;
	m_pMapTiles = NULL;
}

void TERRAIN::Init(IDirect3DDevice9* Dev, INTPOINT _size)
{
	m_pDevice = Dev;
	m_size = _size;
	m_pHeightMap = NULL;

	if(m_pMapTiles != NULL)	//Clear old maptiles
		delete [] m_pMapTiles;

	//Create maptiles
	m_pMapTiles = new MAPTILE[m_size.x * m_size.y];
	memset(m_pMapTiles, 0, sizeof(MAPTILE)*m_size.x*m_size.y);

	//Clear old textures
	for(int i=0;i<(int)m_diffuseMaps.size();i++)
		m_diffuseMaps[i]->Release();
	m_diffuseMaps.clear();

	//Load textures
	IDirect3DTexture9* grass = NULL, *mount = NULL, *snow = NULL;
	if(FAILED(D3DXCreateTextureFromFile(Dev, "textures/grass.jpg", &grass)))debug.Print("Could not load grass.jpg");
	if(FAILED(D3DXCreateTextureFromFile(Dev, "textures/mountain.jpg", &mount)))debug.Print("Could not load mountain.jpg");
	if(FAILED(D3DXCreateTextureFromFile(Dev, "textures/snow.jpg", &snow)))debug.Print("Could not load snow.jpg");
	m_diffuseMaps.push_back(grass);
	m_diffuseMaps.push_back(mount);
	m_diffuseMaps.push_back(snow);
	m_pAlphaMap = NULL;

	//Load pixelshader
	m_terrainPS.Init(Dev, "Shaders/terrain.ps", PIXEL_SHADER);

	//Create white material	
	m_mtrl.Ambient = m_mtrl.Specular = m_mtrl.Diffuse  = D3DXCOLOR(0.5f, 0.5f, 0.5f, 1.0f);
	m_mtrl.Emissive = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);

	GenerateRandomTerrain(9);
}

void TERRAIN::Release()
{
	for(int i=0;i<(int)m_patches.size();i++)
		if(m_patches[i] != NULL)
			m_patches[i]->Release();

	m_patches.clear();

	if(m_pHeightMap != NULL)
	{
		m_pHeightMap->Release();
		delete m_pHeightMap;
		m_pHeightMap = NULL;
	}

	m_objects.clear();
}

void TERRAIN::GenerateRandomTerrain(int numPatches)
{
	try
	{
		Release();

		//Create two heightmaps and multiply them
		m_pHeightMap = new HEIGHTMAP(m_size, 10.0f);
		HEIGHTMAP hm2(m_size, 2.0f);

		m_pHeightMap->CreateRandomHeightMap(rand()%2000, 1.0f, 0.7f, 8);
		hm2.CreateRandomHeightMap(rand()%2000, 2.5f, 0.8f, 3);

		hm2.Cap(hm2.m_maxHeight * 0.4f);

		*m_pHeightMap *= hm2;
		hm2.Release();
		
		//Add objects
		HEIGHTMAP hm3(m_size, 1.0f);
		hm3.CreateRandomHeightMap(rand()%1000, 5.5f, 0.9f, 7);

		for(int y=0;y<m_size.y;y++)
			for(int x=0;x<m_size.x;x++)
			{
				if(m_pHeightMap->GetHeight(x, y) == 0.0f && hm3.GetHeight(x, y) > 0.7f && rand()%6 == 0)
					AddObject(0, INTPOINT(x, y));	//Tree
				else if(m_pHeightMap->GetHeight(x, y) >= 1.0f && hm3.GetHeight(x, y) > 0.9f && rand()%20 == 0)
					AddObject(1, INTPOINT(x, y));	//Stone
			}

		hm3.Release();

		InitPathfinding();
		CreatePatches(numPatches);
		CalculateAlphaMaps();
	}
	catch(...)
	{
		debug.Print("Error in TERRAIN::GenerateRandomTerrain()");
	}
}

void TERRAIN::CreatePatches(int numPatches)
{
	try
	{
		//Clear any old m_patches
		for(int i=0;i<(int)m_patches.size();i++)
			if(m_patches[i] != NULL)
				m_patches[i]->Release();
		m_patches.clear();

		//Create new patches
		for(int y=0;y<numPatches;y++)
			for(int x=0;x<numPatches;x++)
			{
				RECT r = {(int)(x * (m_size.x - 1) / (float)numPatches), 
						  (int)(y * (m_size.y - 1) / (float)numPatches), 
						(int)((x+1) * (m_size.x - 1) / (float)numPatches),
						(int)((y+1) * (m_size.y - 1) / (float)numPatches)};
						
				PATCH *p = new PATCH();
				p->CreateMesh(*this, r, m_pDevice);
				m_patches.push_back(p);
			}
	}
	catch(...)
	{
		debug.Print("Error in TERRAIN::CreatePatches()");
	}
}

void TERRAIN::CalculateAlphaMaps()
{
	//Clear old alpha maps
	if(m_pAlphaMap != NULL)
		m_pAlphaMap->Release();

	//Create new alpha map
	D3DXCreateTexture(m_pDevice, 128, 128, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_pAlphaMap);

	//Lock the texture
	D3DLOCKED_RECT sRect;
	m_pAlphaMap->LockRect(0, &sRect, NULL, NULL);
	BYTE *bytes = (BYTE*)sRect.pBits;
	memset(bytes, 0, 128*sRect.Pitch);		//Clear texture to black

	for(int i=0;i<(int)m_diffuseMaps.size();i++)
		for(int y=0;y<sRect.Pitch / 4;y++)
			for(int x=0;x<sRect.Pitch / 4;x++)
			{
				int terrain_x = (int)(m_size.x * (x / (float)(sRect.Pitch / 4.0f)));
				int terrain_y = (int)(m_size.y * (y / (float)(sRect.Pitch / 4.0f)));
				MAPTILE *tile = GetTile(terrain_x, terrain_y);

				if(tile != NULL && tile->m_type == i)
					bytes[y * sRect.Pitch + x * 4 + i] = 255;
			}

	//Unlock the texture
	m_pAlphaMap->UnlockRect(0);
	
	//D3DXSaveTextureToFile("alpha.bmp", D3DXIFF_BMP, m_pAlphaMap, NULL);
}

void TERRAIN::AddObject(int type, INTPOINT mappos)
{
	D3DXVECTOR3 pos = D3DXVECTOR3((float)mappos.x, m_pHeightMap->GetHeight(mappos), (float)-mappos.y);	
	D3DXVECTOR3 rot = D3DXVECTOR3((rand()%1000 / 1000.0f) * 0.13f, (rand()%1000 / 1000.0f) * 3.0f, (rand()%1000 / 1000.0f) * 0.13f);

	float sca_xz = (rand()%1000 / 1000.0f) * 0.5f + 0.5f;
	float sca_y = (rand()%1000 / 1000.0f) * 1.0f + 0.5f;
	D3DXVECTOR3 sca = D3DXVECTOR3(sca_xz, sca_y, sca_xz);

	m_objects.push_back(OBJECT(type, mappos, pos, rot, sca));
}

void TERRAIN::Render(CAMERA &camera)
{
	//Set render states		
	m_pDevice->SetRenderState(D3DRS_LIGHTING, false);
	m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, true);	
	
	m_pDevice->SetTexture(0, m_pAlphaMap);
	m_pDevice->SetTexture(1, m_diffuseMaps[0]);		//Grass
	m_pDevice->SetTexture(2, m_diffuseMaps[1]);		//Mountain
	m_pDevice->SetTexture(3, m_diffuseMaps[2]);		//Snow
	m_pDevice->SetMaterial(&m_mtrl);

	D3DXMATRIX world;
	D3DXMatrixIdentity(&world);
	m_pDevice->SetTransform(D3DTS_WORLD, &world);

	m_terrainPS.Begin();
		
	for(int p=0;p<(int)m_patches.size();p++)
		if(!camera.Cull(m_patches[p]->m_BBox))
			m_patches[p]->Render();

	m_terrainPS.End();	

	m_pDevice->SetTexture(1, NULL);
	m_pDevice->SetTexture(2, NULL);
	m_pDevice->SetTexture(3, NULL);

	//Render Objects
	for(int i=0;i<(int)m_objects.size();i++)
		if(!camera.Cull(m_objects[i].m_BBox))
			m_objects[i].Render();
}

bool TERRAIN::Within(INTPOINT p)
{
	return p.x >= 0 && p.y >= 0 && p.x < m_size.x && p.y < m_size.y;
}

void TERRAIN::InitPathfinding()
{
	try
	{
		//Read maptile heights & types from heightmap
		for(int y=0;y<m_size.y;y++)
			for(int x=0;x<m_size.x;x++)
			{
				MAPTILE *tile = GetTile(x, y);
				if(m_pHeightMap != NULL)tile->m_height = m_pHeightMap->GetHeight(x, y);
				tile->m_mappos = INTPOINT(x, y);
				
				if(tile->m_height < 0.3f)		 tile->m_type = 0;	//Grass
				else if(tile->m_height < 7.0f) tile->m_type = 1;	//Stone
				else						 tile->m_type = 2;	//Snow
			}

		//Calculate tile cost as a function of the height variance
		for(int y=0;y<m_size.y;y++)		
			for(int x=0;x<m_size.x;x++)
			{
				MAPTILE *tile = GetTile(x, y);

				if(tile != NULL)
				{
					//Possible neighbors
					INTPOINT p[] = {INTPOINT(x-1, y-1), INTPOINT(x, y-1), INTPOINT(x+1, y-1),
									INTPOINT(x-1, y),					  INTPOINT(x+1, y),
									INTPOINT(x-1, y+1), INTPOINT(x, y+1), INTPOINT(x+1, y+1)};

					float variance = 0.0f;
					int nr = 0;

					//For each neighbor
					for(int i=0;i<8;i++)	
						if(Within(p[i]))
						{
							MAPTILE *neighbor = GetTile(p[i]);

							if(neighbor != NULL)
							{
								float v = neighbor->m_height - tile->m_height;
								variance += (v * v);
								nr++;
							}
						}

					//Cost = height variance
					variance /= (float)nr;
					tile->m_cost = variance + 0.1f;
					if(tile->m_cost > 1.0f)tile->m_cost = 1.0f;

					//If the tile cost is less than 1.0f, then we can walk on the tile
					tile->m_walkable = tile->m_cost < 0.5f;
				}
			}

		//Make maptiles with objects on them not walkable
		for(int i=0;i<(int)m_objects.size();i++)
		{
			MAPTILE *tile = GetTile(m_objects[i].m_mappos);
			if(tile != NULL)
			{
				tile->m_walkable = false;
				tile->m_cost = 1.0f;
			}
		}

		//Connect maptiles using the neightbors[] pointers
		for(int y=0;y<m_size.y;y++)		
			for(int x=0;x<m_size.x;x++)
			{
				MAPTILE *tile = GetTile(x, y);
				if(tile != NULL && tile->m_walkable)
				{
					//Clear old connections
					for(int i=0;i<8;i++)
						tile->m_pNeighbors[i] = NULL;

					//Possible neighbors
					INTPOINT p[] = {INTPOINT(x-1, y-1), INTPOINT(x, y-1), INTPOINT(x+1, y-1),
									INTPOINT(x-1, y),					  INTPOINT(x+1, y),
									INTPOINT(x-1, y+1), INTPOINT(x, y+1), INTPOINT(x+1, y+1)};

					//For each neighbor
					for(int i=0;i<8;i++)	
						if(Within(p[i]))
						{
							MAPTILE *neighbor = GetTile(p[i]);

							//Connect tiles if the neighbor is walkable
							if(neighbor != NULL && neighbor->m_walkable)
								tile->m_pNeighbors[i] = neighbor;
						}
				}
			}

		CreateTileSets();
	}
	catch(...)
	{
		debug.Print("Error in InitPathfinding()");
	}	
}

void TERRAIN::CreateTileSets()
{
	try
	{
		int setNo = 0;
		for(int y=0;y<m_size.y;y++)		//Set a unique set for each tile...
			for(int x=0;x<m_size.x;x++)
				m_pMapTiles[x + y * m_size.x].m_set = setNo++;

		bool changed = true;
		while(changed)
		{
			changed = false;

			for(int y=0;y<m_size.y;y++)
				for(int x=0;x<m_size.x;x++)
				{
					MAPTILE *tile = GetTile(x, y);

					//Find the lowest set of a neighbor
					if(tile != NULL && tile->m_walkable)
					{
						for(int i=0;i<8;i++)
							if(tile->m_pNeighbors[i] != NULL &&
								tile->m_pNeighbors[i]->m_walkable &&
								tile->m_pNeighbors[i]->m_set < tile->m_set)
							{
								changed = true;
								tile->m_set = tile->m_pNeighbors[i]->m_set;
							}
					}
				}
		}
	}
	catch(...)
	{
		debug.Print("Error in TERRAIN::CreateTileSets()");
	}
}

float H(INTPOINT a, INTPOINT b)
{
	//return abs(a.x - b.x) + abs(a.y - b.y);
	return a.Distance(b);
}

std::vector<INTPOINT> TERRAIN::GetPath(INTPOINT start, INTPOINT goal)
{
	try
	{
		//Check that the two points are within the bounds of the map
		MAPTILE *startTile = GetTile(start);
		MAPTILE *goalTile = GetTile(goal);

		if(!Within(start) || !Within(goal) || start == goal || startTile == NULL || goalTile == NULL)
			return std::vector<INTPOINT>();

		//Check if a path exists
		if(!startTile->m_walkable || !goalTile->m_walkable || startTile->m_set != goalTile->m_set)
			return std::vector<INTPOINT>();

		//Init Search
		long numTiles = m_size.x * m_size.y;
		for(long l=0;l<numTiles;l++)
		{
			m_pMapTiles[l].f = m_pMapTiles[l].g = FLT_MAX;		//Clear F,G
			m_pMapTiles[l].open = m_pMapTiles[l].closed = false;	//Reset Open and Closed
		}

		std::vector<MAPTILE*> open;				//Create Our Open list
		startTile->g = 0;						//Init our starting point (SP)
		startTile->f = H(start, goal);
		startTile->open = true;
		open.push_back(startTile);				//Add SP to the Open list

		bool found = false;					// Search as long as a path hasnt been found,
		while(!found && !open.empty())		// or there is no more tiles to search
		{												
			MAPTILE * best = open[0];        // Find the best tile (i.e. the lowest F value)
			int bestPlace = 0;
			for(int i=1;i<(int)open.size();i++)
				if(open[i]->f < best->f)
				{
					best = open[i];
					bestPlace = i;
				}
			
			if(best == NULL)break;			//No path found

			open[bestPlace]->open = false;

			// Take the best node out of the Open list
			open.erase(open.begin() + bestPlace);

			if(best->m_mappos == goal)		//If the goal has been found
			{
				std::vector<INTPOINT> p, p2;
				MAPTILE *point = best;

				while(point->m_mappos != start)	// Generate path
				{
					p.push_back(point->m_mappos);
					point = point->m_pParent;
				}

				for(int i=(int)p.size()-1;i!=0;i--)	// Reverse path
					p2.push_back(p[i]);
				p2.push_back(goal);
				return p2;
			}
			else
			{
				for(int i=0;i<8;i++)					// otherwise, check the neighbors of the
					if(best->m_pNeighbors[i] != NULL)	// best tile
					{
						bool inList = false;		// Generate new G and F value
						float newG = best->g + 1.0f;
						float d = H(best->m_mappos, best->m_pNeighbors[i]->m_mappos);
						float newF = newG + H(best->m_pNeighbors[i]->m_mappos, goal) + best->m_pNeighbors[i]->m_cost * 5.0f * d;

						if(best->m_pNeighbors[i]->open || best->m_pNeighbors[i]->closed)
						{
							if(newF < best->m_pNeighbors[i]->f)	// If the new F value is lower
							{
								best->m_pNeighbors[i]->g = newG;	// update the values of this tile
								best->m_pNeighbors[i]->f = newF;
								best->m_pNeighbors[i]->m_pParent = best;								
							}
							inList = true;
						}

						if(!inList)			// If the neighbor tile isn't in the Open or Closed list
						{
							best->m_pNeighbors[i]->f = newF;		//Set the values
							best->m_pNeighbors[i]->g = newG;
							best->m_pNeighbors[i]->m_pParent = best;
							best->m_pNeighbors[i]->open = true;
							open.push_back(best->m_pNeighbors[i]);	//Add it to the open list	
						}
					}

				best->closed = true;		//The best tile has now been searched, add it to the Closed list
			}
		}

		return std::vector<INTPOINT>();		//No path found, return an empty path
		
	}
	catch(...)
	{
		debug.Print("Error in TERRAIN::GetPath()");
		return std::vector<INTPOINT>();
	}
}

MAPTILE* TERRAIN::GetTile(int x, int y)
{
	if(m_pMapTiles == NULL)return NULL;

	try
	{
		return &m_pMapTiles[x + y * m_size.x];
	}
	catch(...)
	{
		return NULL;
	}
}

void TERRAIN::SaveTerrain(char fileName[])
{
	try
	{
		std::ofstream out(fileName, std::ios::binary);		//Binary format

		if(out.good())
		{
			out.write((char*)&m_size, sizeof(INTPOINT));	//Write map size

			//Write all the maptile information needed to recreate the map
			for(int y=0;y<m_size.y;y++)
				for(int x=0;x<m_size.x;x++)
				{
					MAPTILE *tile = GetTile(x, y);
					out.write((char*)&tile->m_type, sizeof(int));			//type
					out.write((char*)&tile->m_height, sizeof(float));		//Height
				}

			//Write all the objects
			int numObjects = (int)m_objects.size();
			out.write((char*)&numObjects, sizeof(int));	 //Num Objects
			for(int i=0;i<(int)m_objects.size();i++)
			{
				out.write((char*)&m_objects[i].m_type, sizeof(int));							//type
				out.write((char*)&m_objects[i].m_mappos, sizeof(INTPOINT));					//mappos
				out.write((char*)&m_objects[i].m_meshInstance.m_pos, sizeof(D3DXVECTOR3));	//Pos
				out.write((char*)&m_objects[i].m_meshInstance.m_rot, sizeof(D3DXVECTOR3));	//Rot
				out.write((char*)&m_objects[i].m_meshInstance.m_sca, sizeof(D3DXVECTOR3));	//Sca
			}
		}

		out.close();
	}
	catch(...)
	{
		debug.Print("Error in TERRAIN::SaveTerrain()");
	}
}

void TERRAIN::LoadTerrain(char fileName[])
{
	try
	{
		std::ifstream in(fileName, std::ios::binary);		//Binary format

		if(in.good())
		{
			Release();	//Release all terrain resources

			in.read((char*)&m_size, sizeof(INTPOINT));	//read map size
		
			if(m_pMapTiles != NULL)	//Clear old maptiles
				delete [] m_pMapTiles;

			//Create new maptiles
			m_pMapTiles = new MAPTILE[m_size.x * m_size.y];
			memset(m_pMapTiles, 0, sizeof(MAPTILE)*m_size.x*m_size.y);


			//Read the maptile information
			for(int y=0;y<m_size.y;y++)
				for(int x=0;x<m_size.x;x++)
				{
					MAPTILE *tile = GetTile(x, y);
					in.read((char*)&tile->m_type, sizeof(int));			//type
					in.read((char*)&tile->m_height, sizeof(float));		//Height
				}

			//Read number of objects
			int numObjects = 0;
			in.read((char*)&numObjects, sizeof(int));
			for(int i=0;i<numObjects;i++)
			{
				int type = 0;
				INTPOINT mp;
				D3DXVECTOR3 p, r, s;

				in.read((char*)&type, sizeof(int));			//type
				in.read((char*)&mp, sizeof(INTPOINT));		//mappos
				in.read((char*)&p, sizeof(D3DXVECTOR3));	//Pos
				in.read((char*)&r, sizeof(D3DXVECTOR3));	//Rot
				in.read((char*)&s, sizeof(D3DXVECTOR3));	//Sca

				m_objects.push_back(OBJECT(type, mp, p, r, s));
			}

			//Recreate Terrain
			InitPathfinding();
			CreatePatches(3);
			CalculateAlphaMaps();
		}

		in.close();
	}
	catch(...)
	{
		debug.Print("Error in TERRAIN::LoadTerrain()");
	}
}

D3DXVECTOR3 TERRAIN::GetWorldPos(INTPOINT mappos)
{
	if(!Within(mappos))return D3DXVECTOR3(0, 0, 0);
	MAPTILE *tile = GetTile(mappos);
	return D3DXVECTOR3((float)mappos.x, tile->m_height, (float)-mappos.y);
}