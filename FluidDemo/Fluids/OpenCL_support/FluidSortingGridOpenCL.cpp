/* FluidSortingGridOpenCL.cpp
	Copyright (C) 2012 Jackson Lee

	ZLib license
	This software is provided 'as-is', without any express or implied
	warranty. In no event will the authors be held liable for any damages
	arising from the use of this software.
	
	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:
	
	1. The origin of this software must not be misrepresented; you must not
	   claim that you wrote the original software. If you use this software
	   in a product, an acknowledgment in the product documentation would be
	   appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be
	   misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.
*/
#include "FluidSortingGridOpenCL.h"

#include "btExperimentsOpenCL/btLauncherCL.h"

#include "../FluidSortingGrid.h"


// /////////////////////////////////////////////////////////////////////////////
//class FluidSortingGrid_OpenCL
// /////////////////////////////////////////////////////////////////////////////
void FluidSortingGrid_OpenCL::writeToOpenCL(cl_context context, cl_command_queue commandQueue, 
											FluidSortingGrid *sortingGrid, bool transferCellProcessingGroups)
{
	int numActiveCells = sortingGrid->getNumGridCells();
	
	if(!m_maxActiveCells || m_maxActiveCells < numActiveCells)
	{
		deallocate();
		allocate(context, commandQueue, numActiveCells);
	}
	
	btAlignedObjectArray<SortGridValue> &activeCells = sortingGrid->internalGetActiveCells();
	btAlignedObjectArray<FluidGridIterator>&cellContents = sortingGrid->internalGetCellContents();
	
	m_buffer_numActiveCells.writeToBuffer( commandQueue, &numActiveCells, sizeof(int) );
	if(numActiveCells)
	{
		m_buffer_activeCells.writeToBuffer( commandQueue, &activeCells[0], sizeof(SortGridValue)*numActiveCells );
		m_buffer_cellContents.writeToBuffer( commandQueue, &cellContents[0], sizeof(FluidGridIterator)*numActiveCells );
	}
	
	if(transferCellProcessingGroups)
	{
		for(int i = 0; i < FluidSortingGrid::NUM_CELL_PROCESSING_GROUPS; ++i)
		{
			if(m_cellProcessingGroups[i])
			{
				const btAlignedObjectArray<int> &group = sortingGrid->internalGetCellProcessingGroup(i);
				m_cellProcessingGroups[i]->copyFromHost(group, false);
			}
		}
		m_adjacentCells->resize(numActiveCells, false);
	}
	
	cl_int error_code = clFinish(commandQueue);
	CHECK_CL_ERROR(error_code);
}
void FluidSortingGrid_OpenCL::readFromOpenCL(cl_context context, cl_command_queue commandQueue, 
											FluidSortingGrid *sortingGrid, bool transferCellProcessingGroups)
{
	btAssert(m_maxActiveCells != 0);
	
	btAlignedObjectArray<SortGridValue> &activeCells = sortingGrid->internalGetActiveCells();
	btAlignedObjectArray<FluidGridIterator>&cellContents = sortingGrid->internalGetCellContents();
	
	int numActiveCells = getNumActiveCells(commandQueue);
	activeCells.resize(numActiveCells);
	cellContents.resize(numActiveCells);
	
	if(numActiveCells)
	{
		m_buffer_activeCells.readFromBuffer( commandQueue, &activeCells[0], sizeof(SortGridValue)*numActiveCells );
		m_buffer_cellContents.readFromBuffer( commandQueue, &cellContents[0], sizeof(FluidGridIterator)*numActiveCells );	
	
		if(transferCellProcessingGroups)
		{
			for(int i = 0; i < FluidSortingGrid::NUM_CELL_PROCESSING_GROUPS; ++i)
			{
				if(m_cellProcessingGroups[i])
				{
					btAlignedObjectArray<int> &group = sortingGrid->internalGetCellProcessingGroup(i);
					m_cellProcessingGroups[i]->copyToHost(group, false);
				}
			}
		}
		else 
		{
			for(int i = 0; i < FluidSortingGrid::NUM_CELL_PROCESSING_GROUPS; ++i) sortingGrid->internalGetCellProcessingGroup(i).resize(0);
		}
		
		cl_int error_code = clFinish(commandQueue);
		CHECK_CL_ERROR(error_code);
	}
	else 
	{
		for(int i = 0; i < FluidSortingGrid::NUM_CELL_PROCESSING_GROUPS; ++i) sortingGrid->internalGetCellProcessingGroup(i).resize(0);
	}
}

int FluidSortingGrid_OpenCL::getNumActiveCells(cl_command_queue commandQueue)
{
	if(!m_maxActiveCells) return 0;

	int numActiveCells;
	m_buffer_numActiveCells.readFromBuffer( commandQueue, &numActiveCells, sizeof(int) );
	
	cl_int errorCode = clFinish(commandQueue);
	CHECK_CL_ERROR(errorCode);
	
	return numActiveCells;
}

void FluidSortingGrid_OpenCL::resize(cl_context context, cl_command_queue commandQueue, int maxGridCells)
{
	deallocate();
	allocate(context, commandQueue, maxGridCells);
}

void FluidSortingGrid_OpenCL::allocate(cl_context context, cl_command_queue commandQueue, int maxGridCells)
{
	m_maxActiveCells = maxGridCells;
	
	m_buffer_numActiveCells.allocate( context, sizeof(int) );
	m_buffer_activeCells.allocate( context, sizeof(SortGridValue)*maxGridCells );
	m_buffer_cellContents.allocate( context, sizeof(FluidGridIterator)*maxGridCells );
	
	for(int i = 0; i < FluidSortingGrid::NUM_CELL_PROCESSING_GROUPS; ++i)
	{
		if(!m_cellProcessingGroups[i])
		{
			void *ptr = btAlignedAlloc( sizeof(btOpenCLArray<int>), 16 );
			m_cellProcessingGroups[i] = new(ptr) btOpenCLArray<int>(context, commandQueue);
		}
	}
	
	if(!m_adjacentCells)
	{
		void *ptr = btAlignedAlloc( sizeof(btOpenCLArray<FoundCellsSymmetric>), 16 );
		m_adjacentCells = new(ptr) btOpenCLArray<FoundCellsSymmetric>(context, commandQueue);
	}
	
	const int NUM_ACTIVE_CELLS = 0;
	m_buffer_numActiveCells.writeToBuffer( commandQueue, &NUM_ACTIVE_CELLS, sizeof(int) );
	
	cl_int errorCode = clFinish(commandQueue);
	CHECK_CL_ERROR(errorCode);
}
void FluidSortingGrid_OpenCL::deallocate()
{
	m_maxActiveCells = 0;
	
	m_buffer_numActiveCells.deallocate();
	m_buffer_activeCells.deallocate();
	m_buffer_cellContents.deallocate();
	
	for(int i = 0; i < FluidSortingGrid::NUM_CELL_PROCESSING_GROUPS; ++i)
	{
		if(m_cellProcessingGroups[i])
		{
			m_cellProcessingGroups[i]->~btOpenCLArray<int>();
			btAlignedFree(m_cellProcessingGroups[i]);
			m_cellProcessingGroups[i] = 0;
		}
		
	}
	
	if(m_adjacentCells)
	{
		m_adjacentCells->~btOpenCLArray<FoundCellsSymmetric>();
		btAlignedFree(m_adjacentCells);
		m_adjacentCells = 0;
	}
}


// /////////////////////////////////////////////////////////////////////////////
//class FluidSortingGrid_OpenCL_Program
// /////////////////////////////////////////////////////////////////////////////
FluidSortingGrid_OpenCL_Program::FluidSortingGrid_OpenCL_Program()
{
	sortingGrid_program = 0;
	kernel_generateValueIndexPairs = 0;
	kernel_rearrangeParticleArrays = 0;
	kernel_generateUniques = 0;
	
	m_radixSorter = 0;
	m_valueIndexPairs = 0;
}

void FluidSortingGrid_OpenCL_Program::initialize(cl_context context, cl_device_id gpu_device, cl_command_queue queue)
{
	cl_int error_code;
	
	//Program
	const char CL_SORTING_GRID_PROGRAM_PATH[] = "./Demos/FluidDemo/Fluids/OpenCL_support/fluids.cl";
	sortingGrid_program = compileProgramOpenCL(context, gpu_device, CL_SORTING_GRID_PROGRAM_PATH);

	//Kernels
	kernel_generateValueIndexPairs = clCreateKernel(sortingGrid_program, "generateValueIndexPairs", &error_code);
	CHECK_CL_ERROR(error_code);
	kernel_rearrangeParticleArrays = clCreateKernel(sortingGrid_program, "rearrangeParticleArrays", &error_code);
	CHECK_CL_ERROR(error_code);
	kernel_generateUniques = clCreateKernel(sortingGrid_program, "generateUniques", &error_code);
	CHECK_CL_ERROR(error_code);
	
	//
	if(!m_radixSorter)
	{
		void *ptr = btAlignedAlloc( sizeof(btRadixSort32CL), 16 );
		m_radixSorter = new(ptr) btRadixSort32CL(context, gpu_device, queue);
		
		ptr = btAlignedAlloc( sizeof(btOpenCLArray<btSortData>), 16 );
		m_valueIndexPairs = new(ptr) btOpenCLArray<btSortData>(context, queue);
	}
}
void FluidSortingGrid_OpenCL_Program::deactivate()
{
	cl_int error_code;

	//Kernels
	error_code = clReleaseKernel(kernel_generateValueIndexPairs);
	CHECK_CL_ERROR(error_code);
	error_code = clReleaseKernel(kernel_rearrangeParticleArrays);
	CHECK_CL_ERROR(error_code);
	error_code = clReleaseKernel(kernel_generateUniques);
	CHECK_CL_ERROR(error_code);
		
	//Program
	error_code = clReleaseProgram(sortingGrid_program);
	CHECK_CL_ERROR(error_code);
	
	//Buffers
	buffer_temp.deallocate();
	
	//
	sortingGrid_program = 0;
	kernel_generateValueIndexPairs = 0;
	kernel_rearrangeParticleArrays = 0;
	kernel_generateUniques = 0;
	
	//
	if(m_radixSorter)
	{
		m_radixSorter->~btRadixSort32CL();
		btAlignedFree(m_radixSorter);
		m_radixSorter = 0;
		
		m_valueIndexPairs->~btOpenCLArray<btSortData>();
		btAlignedFree(m_valueIndexPairs);
		m_valueIndexPairs = 0;
	}
}


void rearrangeToMatchSortedValues2(const btAlignedObjectArray<btSortData> &sortedValues, btAlignedObjectArray<btVector3> &out_rearranged)
{
	static btAlignedObjectArray<btVector3> result;
	result.resize( out_rearranged.size() );
	
	for(int i = 0; i < out_rearranged.size(); ++i)
	{
		int oldIndex = sortedValues[i].m_value;
		int newIndex = i;
			
		result[newIndex] = out_rearranged[oldIndex];
	}
	
	out_rearranged = result;
}
void FluidSortingGrid_OpenCL_Program::insertParticlesIntoGrid(cl_context context, cl_command_queue commandQueue,
															  FluidSph *fluid, Fluid_OpenCL *fluidData, FluidSortingGrid_OpenCL *gridData)
{
	BT_PROFILE("FluidSortingGrid_OpenCL_Program::insertParticlesIntoGrid()");

	btAssert(m_radixSorter);
	btAssert(m_valueIndexPairs);
	cl_int error_code;
	
	int numFluidParticles = fluid->numParticles();
	
	//
	unsigned int fluidParticlesSize = sizeof(btVector3)*numFluidParticles;
	if( buffer_temp.getSize() < fluidParticlesSize )
	{
		buffer_temp.deallocate();
		buffer_temp.allocate(context, fluidParticlesSize);
	}
	
	if( gridData->getMaxActiveCells() < numFluidParticles ) gridData->resize(context, commandQueue, numFluidParticles);
	
	if( m_valueIndexPairs->size() != numFluidParticles ) m_valueIndexPairs->resize(numFluidParticles, true);
	
	//
	{
		BT_PROFILE("generateValueIndexPairs()");
		generateValueIndexPairs( commandQueue, numFluidParticles, fluid->getGrid().getCellSize(), fluidData->m_buffer_pos.getBuffer() );
		
		error_code = clFinish(commandQueue);
		CHECK_CL_ERROR(error_code);
	}
	
	//Note that btRadixSort32CL uses btSortData, while FluidSortingGrid uses ValueIndexPair.
	//btSortData.m_key == ValueIndexPair.m_value (value to sort by)
	//btSortData.m_value == ValueIndexPair.m_index (fluid particle index)
	{
		BT_PROFILE("radix sort");
		m_radixSorter->execute(*m_valueIndexPairs, 32);
	}
	
	//
	{
		BT_PROFILE("rearrange device");
		
		rearrangeParticleArrays( commandQueue, numFluidParticles, fluidData->m_buffer_pos.getBuffer() );
		error_code = clEnqueueCopyBuffer( commandQueue, buffer_temp.getBuffer(), fluidData->m_buffer_pos.getBuffer(), 
										  0, 0, sizeof(btVector3)*numFluidParticles, 0, 0, 0 );
		CHECK_CL_ERROR(error_code);
		
		rearrangeParticleArrays( commandQueue, numFluidParticles, fluidData->m_buffer_vel_eval.getBuffer() );
		error_code = clEnqueueCopyBuffer( commandQueue, buffer_temp.getBuffer(), fluidData->m_buffer_vel_eval.getBuffer(), 
											0, 0, sizeof(btVector3)*numFluidParticles, 0, 0, 0 );
		CHECK_CL_ERROR(error_code);
		
		error_code = clFinish(commandQueue);
		CHECK_CL_ERROR(error_code);
	}
	
	{
		BT_PROFILE("rearrange host");
		m_valueIndexPairs->copyToHost(m_valueIndexPairsHost, true);
		
		FluidParticles &particles = fluid->internalGetFluidParticles();
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, particles.m_pos);
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, particles.m_vel);
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, particles.m_vel_eval);
		rearrangeToMatchSortedValues2(m_valueIndexPairsHost, particles.m_externalAcceleration);
	}
	
	//
	{
		BT_PROFILE("generateUniques_serial()");
		generateUniques(commandQueue, numFluidParticles, gridData);
		
		error_code = clFinish(commandQueue);
		CHECK_CL_ERROR(error_code);
	}
}

void FluidSortingGrid_OpenCL_Program::generateValueIndexPairs(cl_command_queue commandQueue, int numFluidParticles, 
															  btScalar cellSize, cl_mem fluidPositionsBuffer)
{
	btBufferInfoCL bufferInfo[] = 
	{
		btBufferInfoCL( fluidPositionsBuffer ),
		btBufferInfoCL( m_valueIndexPairs->getBufferCL() )
	};
	
	btLauncherCL launcher(commandQueue, kernel_generateValueIndexPairs);
	launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
	launcher.setConst(cellSize);
	
	launcher.launchAutoSizedWorkGroups1D(numFluidParticles);
}
void FluidSortingGrid_OpenCL_Program::rearrangeParticleArrays(cl_command_queue commandQueue, int numFluidParticles, cl_mem fluidBuffer)
{
	btBufferInfoCL bufferInfo[] = 
	{
		btBufferInfoCL( m_valueIndexPairs->getBufferCL() ),
		btBufferInfoCL( fluidBuffer ),
		btBufferInfoCL( buffer_temp.getBuffer() )
	};
	
	btLauncherCL launcher(commandQueue, kernel_rearrangeParticleArrays);
	launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
	
	launcher.launchAutoSizedWorkGroups1D(numFluidParticles);
}

void FluidSortingGrid_OpenCL_Program::generateUniques(cl_command_queue commandQueue, int numFluidParticles, FluidSortingGrid_OpenCL *gridData)
{
	btBufferInfoCL bufferInfo[] = 
	{
		btBufferInfoCL( m_valueIndexPairs->getBufferCL() ),
		btBufferInfoCL( gridData->m_buffer_activeCells.getBuffer() ),
		btBufferInfoCL( gridData->m_buffer_cellContents.getBuffer() ),
		btBufferInfoCL( gridData->m_buffer_numActiveCells.getBuffer() )
	};
	
	btLauncherCL launcher(commandQueue, kernel_generateUniques);
	launcher.setBuffers( bufferInfo, sizeof(bufferInfo)/sizeof(btBufferInfoCL) );
	launcher.setConst(numFluidParticles);
	
	launcher.launchAutoSizedWorkGroups1D(1);
}
