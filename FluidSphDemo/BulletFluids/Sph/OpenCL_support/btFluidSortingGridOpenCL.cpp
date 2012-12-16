/*
Bullet-FLUIDS 
Copyright (c) 2012 Jackson Lee

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. 
   If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/
#include "btFluidSortingGridOpenCL.h"

#include "btExperimentsOpenCL/btLauncherCL.h"

#include "../btFluidSortingGrid.h"

#include "btFluidSphOpenCL.h"

// /////////////////////////////////////////////////////////////////////////////
//class btFluidSortingGridOpenCL
// /////////////////////////////////////////////////////////////////////////////
void btFluidSortingGridOpenCL::writeToOpenCL(cl_command_queue queue, btFluidSortingGrid& sortingGrid)
{
	int numActiveCells = sortingGrid.internalGetActiveCells().size();

	m_numActiveCells.resize(1);
	m_numActiveCells.copyFromHostPointer(&numActiveCells, 1, 0, false);
	
	m_activeCells.copyFromHost( sortingGrid.internalGetActiveCells(), false );
	m_cellContents.copyFromHost( sortingGrid.internalGetCellContents(), false );
	
	clFinish(queue);
}
void btFluidSortingGridOpenCL::readFromOpenCL(cl_command_queue queue, btFluidSortingGrid& sortingGrid)
{
	m_activeCells.copyToHost( sortingGrid.internalGetActiveCells(), false );
	m_cellContents.copyToHost( sortingGrid.internalGetCellContents(), false );
	
	clFinish(queue);
}

int btFluidSortingGridOpenCL::getNumActiveCells() const
{
	int numActiveCells;
	m_numActiveCells.copyToHostPointer(&numActiveCells, 1, 0, true);
	
	return numActiveCells;
}


// /////////////////////////////////////////////////////////////////////////////
//class btFluidSortingGridOpenCLProgram_GenerateUniques
// /////////////////////////////////////////////////////////////////////////////
btFluidSortingGridOpenCLProgram_GenerateUniques::btFluidSortingGridOpenCLProgram_GenerateUniques
(
	cl_context context, 
	cl_device_id device,
	cl_command_queue queue
)
: m_prefixScanner(context, device, queue), m_tempInts(context, queue), m_scanResults(context, queue)
{
	const char CL_SORTING_GRID_PROGRAM_PATH[] = "./Demos/FluidSphDemo/BulletFluids/Sph/OpenCL_support/fluids.cl";
	sortingGrid_program = compileProgramOpenCL(context, device, CL_SORTING_GRID_PROGRAM_PATH);
	btAssert(sortingGrid_program);

	kernel_markUniques = clCreateKernel(sortingGrid_program, "markUniques", 0);
	btAssert(kernel_markUniques);
	kernel_storeUniques = clCreateKernel(sortingGrid_program, "storeUniques", 0);
	btAssert(kernel_storeUniques);
	kernel_setZero = clCreateKernel(sortingGrid_program, "setZero", 0);
	btAssert(kernel_setZero);
	kernel_countUniques = clCreateKernel(sortingGrid_program, "countUniques", 0);
	btAssert(kernel_countUniques);
	kernel_generateIndexRanges = clCreateKernel(sortingGrid_program, "generateIndexRanges", 0);
	btAssert(kernel_generateIndexRanges);
}
btFluidSortingGridOpenCLProgram_GenerateUniques::~btFluidSortingGridOpenCLProgram_GenerateUniques()
{
	clReleaseKernel(kernel_markUniques);
	clReleaseKernel(kernel_storeUniques);
	clReleaseKernel(kernel_setZero);
	clReleaseKernel(kernel_countUniques);
	clReleaseKernel(kernel_generateIndexRanges);
	clReleaseProgram(sortingGrid_program);
}

void btFluidSortingGridOpenCLProgram_GenerateUniques::generateUniques(cl_command_queue commandQueue,
																	const btOpenCLArray<btSortData>& valueIndexPairs,
																	btFluidSortingGridOpenCL* gridData, int numFluidParticles)
{
	BT_PROFILE("generateUniques_parallel()");

	if( m_tempInts.size() < numFluidParticles ) m_tempInts.resize(numFluidParticles, false);
	if( m_scanResults.size() < numFluidParticles ) m_scanResults.resize(numFluidParticles, false);
	
	btOpenCLArray<int>& out_numActiveCells = gridData->m_numActiveCells;
	btOpenCLArray<btSortGridValue>& out_sortGridValues = gridData->m_activeCells;
	btOpenCLArray<btFluidGridIterator>& out_iterators = gridData->m_cellContents;
	
	unsigned int numUniques = 0;
	
	//Detect unique values
	{
		//If the element to the right is different(or out of bounds), set 1; set 0 otherwise
		{
			btBufferInfoCL bufferInfo[] = 
			{
				btBufferInfoCL( valueIndexPairs.getBufferCL() ),
				btBufferInfoCL( m_tempInts.getBufferCL() )
			};
	
			btLauncherCL launcher(commandQueue, kernel_markUniques);
			launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
			launcher.setConst(numFluidParticles - 1);
			
			launcher.launchAutoSizedWorkGroups1D(numFluidParticles);
		}
		
		//
		{
			m_prefixScanner.execute(m_tempInts, m_scanResults, numFluidParticles, &numUniques);		//Exclusive scan
			++numUniques;	//Prefix scanner returns last index if the array is filled with 1 and 0; add 1 to get size
			
			int numActiveCells = static_cast<int>(numUniques);
			out_numActiveCells.copyFromHostPointer(&numActiveCells, 1, 0, true);
			out_sortGridValues.resize(numActiveCells, false);
			out_iterators.resize(numActiveCells, false);
		}
		
		//Use scan results to store unique btSortGridValue
		{
			btBufferInfoCL bufferInfo[] = 
			{
				btBufferInfoCL( valueIndexPairs.getBufferCL() ),
				btBufferInfoCL( m_tempInts.getBufferCL() ),
				btBufferInfoCL( m_scanResults.getBufferCL() ),
				btBufferInfoCL( out_sortGridValues.getBufferCL() )
			};
			
			btLauncherCL launcher(commandQueue, kernel_storeUniques);
			launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
			
			launcher.launchAutoSizedWorkGroups1D(numFluidParticles);
		}
	}
	
	//Count number of each unique value and use count to generate index ranges
	{
		//Set m_tempInts to 0; it will store the number of particles corresponding to each btSortGridValue(num particles in each cell)
		{
			btBufferInfoCL bufferInfo[] = { btBufferInfoCL( m_tempInts.getBufferCL() ) };
		
			btLauncherCL launcher(commandQueue, kernel_setZero);
			launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
			launcher.launchAutoSizedWorkGroups1D(numUniques);
		}
		
		//Use binary search and atomic increment to count the number of particles in each cell
		{
			btBufferInfoCL bufferInfo[] = 
			{
				btBufferInfoCL( valueIndexPairs.getBufferCL() ),
				btBufferInfoCL( out_sortGridValues.getBufferCL() ),
				btBufferInfoCL( m_tempInts.getBufferCL() )
			};
			
			btLauncherCL launcher(commandQueue, kernel_countUniques);
			launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
			launcher.setConst(numUniques);
			
			launcher.launchAutoSizedWorkGroups1D(numFluidParticles);
		}
		
		//Exclusive scan
		m_prefixScanner.execute(m_tempInts, m_scanResults, numUniques, 0);	
		
		//Use scan results to generate index ranges(check element to the right)
		{
			btBufferInfoCL bufferInfo[] = 
			{
				btBufferInfoCL( m_scanResults.getBufferCL() ),
				btBufferInfoCL( out_iterators.getBufferCL() )
			};
			
			btLauncherCL launcher(commandQueue, kernel_generateIndexRanges);
			launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
			launcher.setConst( static_cast<int>(numUniques) );
			launcher.setConst(numFluidParticles);
			
			launcher.launchAutoSizedWorkGroups1D(numUniques);
		}
	}
}
	
// /////////////////////////////////////////////////////////////////////////////
//class btFluidSortingGridOpenCLProgram
// /////////////////////////////////////////////////////////////////////////////
btFluidSortingGridOpenCLProgram::btFluidSortingGridOpenCLProgram(cl_context context, cl_command_queue queue, cl_device_id device)
: m_tempBufferCL(context, queue), m_radixSorter(context, device, queue), 
	m_valueIndexPairs(context, queue), m_generateUniquesProgram(context, device, queue)
{
	const char CL_SORTING_GRID_PROGRAM_PATH[] = "./Demos/FluidSphDemo/BulletFluids/Sph/OpenCL_support/fluids.cl";
	sortingGrid_program = compileProgramOpenCL(context, device, CL_SORTING_GRID_PROGRAM_PATH);
	btAssert(sortingGrid_program);

	kernel_generateValueIndexPairs = clCreateKernel(sortingGrid_program, "generateValueIndexPairs", 0);
	btAssert(kernel_generateValueIndexPairs);
	kernel_rearrangeParticleArrays = clCreateKernel(sortingGrid_program, "rearrangeParticleArrays", 0);
	btAssert(kernel_rearrangeParticleArrays);
	kernel_generateUniques = clCreateKernel(sortingGrid_program, "generateUniques", 0);
	btAssert(kernel_generateUniques);
}
btFluidSortingGridOpenCLProgram::~btFluidSortingGridOpenCLProgram()
{
	clReleaseKernel(kernel_generateValueIndexPairs);
	clReleaseKernel(kernel_rearrangeParticleArrays);
	clReleaseKernel(kernel_generateUniques);
	clReleaseProgram(sortingGrid_program);
}

template<typename T>
void rearrangeToMatchSortedValues2(const btAlignedObjectArray<btSortData>& sortedValues, 
									btAlignedObjectArray<T>& temp, btAlignedObjectArray<T>& out_rearranged)
{
	temp.resize( out_rearranged.size() );
	
	for(int i = 0; i < out_rearranged.size(); ++i)
	{
		int oldIndex = sortedValues[i].m_value;
		int newIndex = i;
			
		temp[newIndex] = out_rearranged[oldIndex];
	}
	
	out_rearranged = temp;
}
void btFluidSortingGridOpenCLProgram::insertParticlesIntoGrid(cl_context context, cl_command_queue commandQueue,
															  btFluidSph* fluid, btFluidSphOpenCL* fluidData, btFluidSortingGridOpenCL* gridData)
{
	BT_PROFILE("btFluidSortingGridOpenCLProgram::insertParticlesIntoGrid()");
	
	int numFluidParticles = fluid->numParticles();
	m_tempBufferCL.resize(numFluidParticles);
	m_valueIndexPairs.resize(numFluidParticles);
	
	//Cannot check number of nonempty grid cells before generateUniques();
	//temporarily resize m_activeCells and m_cellContents
	//to handle the case where each particle occupies a different grid cell.
	gridData->m_numActiveCells.resize(1);
	gridData->m_activeCells.resize(numFluidParticles);
	gridData->m_cellContents.resize(numFluidParticles);
	
	//
	{
		BT_PROFILE("generateValueIndexPairs()");
		generateValueIndexPairs( commandQueue, numFluidParticles, fluid->getGrid().getCellSize(), fluidData->m_pos.getBufferCL() );
		
		clFinish(commandQueue);
	}
	
	//Note that btRadixSort32CL uses btSortData, while btFluidSortingGrid uses btValueIndexPair.
	//btSortData.m_key == btValueIndexPair.m_value (value to sort by)
	//btSortData.m_value == btValueIndexPair.m_index (fluid particle index)
	{
		BT_PROFILE("radix sort");
		m_radixSorter.execute(m_valueIndexPairs, 32);
	}
	
	//
	{
		BT_PROFILE("rearrange device");
		
		rearrangeParticleArrays( commandQueue, numFluidParticles, fluidData->m_pos.getBufferCL() );
		fluidData->m_pos.copyFromOpenCLArray(m_tempBufferCL);
		
		rearrangeParticleArrays( commandQueue, numFluidParticles, fluidData->m_vel_eval.getBufferCL() );
		fluidData->m_vel_eval.copyFromOpenCLArray(m_tempBufferCL);
		
		clFinish(commandQueue);
	}
	
	{
		BT_PROFILE("rearrange host");
		m_valueIndexPairs.copyToHost(m_valueIndexPairsHost, true);
		
		btFluidParticles& particles = fluid->internalGetParticles();
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, m_tempBufferVector, particles.m_pos);
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, m_tempBufferVector, particles.m_vel);
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, m_tempBufferVector, particles.m_vel_eval);
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, m_tempBufferVector, particles.m_accumulatedForce);
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, m_tempBufferVoid, particles.m_userPointer);
	}
	
	//
	const bool USE_PARALLEL_GENERATE_UNIQUES = true;
	if(USE_PARALLEL_GENERATE_UNIQUES)
	{
		m_generateUniquesProgram.generateUniques(commandQueue, m_valueIndexPairs, gridData, numFluidParticles);
		clFinish(commandQueue);
	}
	else
	{
		BT_PROFILE("generateUniques_serial()");
		generateUniques_serial(commandQueue, numFluidParticles, gridData);
		
		int numActiveCells = gridData->getNumActiveCells();
		gridData->m_activeCells.resize(numActiveCells);
		gridData->m_cellContents.resize(numActiveCells);
		clFinish(commandQueue);
	}
}

void btFluidSortingGridOpenCLProgram::generateValueIndexPairs(cl_command_queue commandQueue, int numFluidParticles, 
															  btScalar cellSize, cl_mem fluidPositionsBuffer)
{
	btBufferInfoCL bufferInfo[] = 
	{
		btBufferInfoCL( fluidPositionsBuffer ),
		btBufferInfoCL( m_valueIndexPairs.getBufferCL() )
	};
	
	btLauncherCL launcher(commandQueue, kernel_generateValueIndexPairs);
	launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
	launcher.setConst(cellSize);
	
	launcher.launchAutoSizedWorkGroups1D(numFluidParticles);
}
void btFluidSortingGridOpenCLProgram::rearrangeParticleArrays(cl_command_queue commandQueue, int numFluidParticles, cl_mem fluidBuffer)
{
	btBufferInfoCL bufferInfo[] = 
	{
		btBufferInfoCL( m_valueIndexPairs.getBufferCL() ),
		btBufferInfoCL( fluidBuffer ),
		btBufferInfoCL( m_tempBufferCL.getBufferCL() )
	};
	
	btLauncherCL launcher(commandQueue, kernel_rearrangeParticleArrays);
	launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
	
	launcher.launchAutoSizedWorkGroups1D(numFluidParticles);
}

void btFluidSortingGridOpenCLProgram::generateUniques_serial(cl_command_queue commandQueue, int numFluidParticles, btFluidSortingGridOpenCL* gridData)
{
	btBufferInfoCL bufferInfo[] = 
	{
		btBufferInfoCL( m_valueIndexPairs.getBufferCL() ),
		btBufferInfoCL( gridData->m_activeCells.getBufferCL() ),
		btBufferInfoCL( gridData->m_cellContents.getBufferCL() ),
		btBufferInfoCL( gridData->m_numActiveCells.getBufferCL() )
	};
	
	btLauncherCL launcher(commandQueue, kernel_generateUniques);
	launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
	launcher.setConst(numFluidParticles);
	
	launcher.launchAutoSizedWorkGroups1D(1);
}