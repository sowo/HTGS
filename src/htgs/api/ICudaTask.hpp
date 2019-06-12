
// NIST-developed software is provided by NIST as a public service. You may use, copy and distribute copies of the software in any medium, provided that you keep intact this entire notice. You may improve, modify and create derivative works of the software or any portion of the software, and you may copy and distribute such modifications or works. Modified works should carry a notice stating that you changed the software and should note the date and nature of any such change. Please explicitly acknowledge the National Institute of Standards and Technology as the source of the software.
// NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY OF ANY KIND, EXPRESS, IMPLIED, IN FACT OR ARISING BY OPERATION OF LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST NEITHER REPRESENTS NOR WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE CORRECTED. NIST DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE SOFTWARE OR THE RESULTS THEREOF, INCLUDING BUT NOT LIMITED TO THE CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE.
// You are solely responsible for determining the appropriateness of using and distributing the software and you assume all risks associated with its use, including but not limited to the risks and costs of program errors, compliance with applicable laws, damage to or loss of data, programs or equipment, and the unavailability or interruption of operation. This software is not intended to be used in any situation where a failure could cause risk of injury or damage to property. The software developed by NIST employees is not subject to copyright protection within the United States.

/**
 * @file ICudaTask.hpp
 * @author Timothy Blattner
 * @date Dec 1, 2015
 *
 * @brief ICudaTask.hpp is used to define an NVIDIA Cuda GPU Tasks
 */

#ifdef USE_CUDA
#ifndef HTGS_CUDATASK_HPP
#define HTGS_CUDATASK_HPP

#include <cuda_runtime_api.h>

#include <vector>
#include <unordered_map>
#include <algorithm>

#include <htgs/api/ITask.hpp>
namespace htgs {

template<class T>
class MemoryData;

/**
 * @class ICudaTask ICudaTask.hpp <htgs/api/ICudaTask.hpp>
 * @brief An ICudaTask is used to attach a task to an NVIDIA Cuda GPU.
 *
 * The task that inherits from this class will automatically be attached to
 * the GPU when launched by the TaskGraphRunTime from within a TaskGraphConf.
 *
 * An ICudaTask may be bound to one or more GPUs if the task is added into an ExecutionPipeline.
 * The number of CUContexts must match the number of pipelines specified for the ExecutionPipeline.
 *
 * Mechanisms to handle automatic data motion for GPU-to-GPU memories
 * is provided to simplify peer to peer device memory copies.
 * In order to use peer to peer copy, both GPUs must reside on the
 * same I/O Hub (IOH) and be the same GPU model.
 * 
 * It may be necessary to copy data that resides on two different GPUs. This can be achieved by using the 
 * autoCopy(V destination, std::shared_ptr<MemoryData<V>> data, long numElems) function.
 * This occurs when there are ghost regions between data domains. If peer to peer copying is allowed
 * between the multiple GPUs, then the autocopy function is not needed. See below for an example of using autocopy.
 *
 * At this time it is necessary for the ICudaTask to copy data from CPU memories to GPU memories.
 *
 * Functions are available for getting the CUDA stream, context, pipeline ID, and number of pipelines.
 *
 * @note It is ideal to configure a separate copy ICudaTask to copy data asynchronously from a computation ICudaTask for CPU->GPU or GPU->CPU copies.
 * 
 * Example implementation:
 * @code
 *
 * #define SIZE 100
 *
 * class SimpleCudaTask : public htgs::ICudaTask<MatrixData, VoidData> {
 * public:
 * SimpleCudaTask(int *cudaIds, int numGpus) : ICudaTask(contexts, cudaIds, numGpus) { }
 * ~SimpleCudaTask() {}
 * virtual void initializeCudaGPU()
 * {
 *    // Allocate local GPU memory in initialize will allocate on correct GPU
 *    cudaMalloc(&localMemory, sizeof(double) * SIZE);
 * }
 *
 * virtual void executeTask(std::shared_ptr<MatrixData> data) {
 *   ...
 *   double * memory;
 *
 *   // Checks if the data received needs to be copied to another GPU
 *   // getCudaMemoryData is defined by the MatrixData class
 *   if (this->autoCopy(localMemory, data->getCudaMemoryData(), SIZE))
 *   {
 *     // Copy was required
 *     memory = localMemory;
 *   }
 *   else
 *   {
 *     // Copy was not required because of peer to peer or same GPU
 *     memory = data->getMemoryData()->get();
 *
 *   }
 *   ...
 * }
 *
 * virtual void shutdownCuda() { cudaFree(localMemory); }
 * virtual void debug() { ... }
 * virtual std::string getName() { return "SimpleCudaTask"; }
 * virtual htgs::ITask<PCIAMData, CCFData> *copy() { return new SimpleCudaTask(...) }
 *
 * private:
 *   double *localMemory;
 *
 * };
 * @endcode
 *
 * Example usage:
 * @code
 *
 * htgs::TaskGraphConf<MatrixData, htgs::VoidData> *taskGraph = new htgs::TaskGraphConf<MatrixData, htgs::VoidData>();
 *
 * SimpleCudaTask *cudaTask = new SimpleCudaTask(...);
 *
 * // Adds cudaTask to process input from taskGraph, input type of cudaTask matches input type of taskGraph
 * taskGraph->setGraphConsumerTask(cudaTask);
 *
 *
 *
 * @endcode
 *
 * @tparam T the input data type for the ICudaTask ITask, T must derive from IData.
 * @tparam U the output data type for the ICudaTask ITask, U must derive from IData.
 */
template<class T, class U>
class ICudaTask : public ITask<T, U> {
  static_assert(std::is_base_of<IData, T>::value, "T must derive from IData");
  static_assert(std::is_base_of<IData, U>::value, "U must derive from IData");

 public:

  /**
   * Creates an ICudaTask.
   * If this task is added into an ExecutionPipeline, then the number of cudaIds
   * should match the number of pipelines
   *
   * @param cudaIds the array of cudaIds
   * @param numGpus the number of GPUs
   * @param autoEnablePeerAccess Flag to automatically enables peer access between multiple GPUs (default true)
   */
  ICudaTask(int *cudaIds, size_t numGpus, bool autoEnablePeerAccess = true) {
    this->cudaIds = cudaIds;
    this->numGpus = numGpus;
  }

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////// VIRTUAL FUNCTIONS ///////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  virtual ~ICudaTask() override {}

  /**
   * Virtual function that is called when the ICudaTask has been initialized and is bound to a CUDA GPU.
   */
  virtual void initializeCudaGPU() {}

  /**
   * Executes the ICudaTask on some data. Use this->getStream() to acquire CUDA stream if needed.
   * @param data the data executed on
   */
  virtual void executeTask(std::shared_ptr<T> data) = 0;

  /**
   * Virtual function that is called when the ICudaTask is shutting down
   */
  virtual void shutdownCuda() {}

  /**
   * Virtual function that gets the name of this ICudaTask
   * @return the name of the ICudaTask
   */
  virtual std::string getName() override {
    return "Unnamed GPU ITask";
  }

  std::string getDotFillColor() override {
    return "green3";
  }

//  virtual std::string genDot(int flags, std::string dotId) override {
//    std::string inOutLabel = (((DOTGEN_FLAG_SHOW_IN_OUT_TYPES & flags) != 0) ? ("\nin: "+this->inTypeName()+"\nout: "+this->outTypeName()) : "");
//    std::string threadLabel = (((flags & DOTGEN_FLAG_SHOW_ALL_THREADING) != 0) ? "" : (" x" + std::to_string(this->getNumThreads())));
//    return dotId + "[label=\"" + this->getName()  + threadLabel + inOutLabel + "\",style=filled,fillcolor=forestgreen,shape=box,color=black,width=.2,height=.2];\n";
//  }

  /**
   * Pure virtual function that copies this ICudaTask
   * @return the copy of the ICudaTask
   */
  virtual ITask <T, U> *copy() = 0;

  /**
   * Virtual function that can be used to provide debug information.
   */
  virtual void debug() override {}

  ////////////////////////////////////////////////////////////////////////////////
  //////////////////////// CLASS FUNCTIONS ///////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  /**
   * Gets the Cuda Id for this cudaTask.
   * Set only after this task has been bound to a thread during initialization.
   * @return the cudaId associated with this cudaTask
   */
  int getCudaId() {
    return this->cudaId;
  }

  /**
   * Checks if the requested pipelineId requires GPU-to-GPU copy
   * @param pipelineId the ExecutionPipeline id
   * @return whether the requested pipelineId would require a GPU-to-GPU copy
   * @retval TRUE if copy is required
   * @retval FALSE if copy is not required
   */
  bool requiresCopy(size_t pipelineId) {
    return std::find(this->nonPeerDevIds.begin(), this->nonPeerDevIds.end(),
      this->cudaIds[pipelineId]) != this->nonPeerDevIds.end();
  }

  /**
   * Checks if the requested pipelineId requires GPU-to-GPU copy
   * @param data the memory data to check
   * @return whether the requested MemoryData would require GPU-to-GPU copy
   * @retval TRUE if copy is required
   * @retval FALSE if copy is not required
   * @tparam V a type of MemoryData that is allocated using a CudaMemoryManager (created using taskGraph->addCudaMemoryEdge)
   */
  template<class V>
  bool requiresCopy(std::shared_ptr<MemoryData<V>> data) {
    return this->requiresCopy(data->getPipelineId());
  }

  /**
   * Checks if the requested pipelineId allows peer to peer GPU copy
   * @param pipelineId the pipelineId to check
   * @return Whether the pipeline id has peer to peer GPU copy
   * @retval TRUE if the pipeline id has peer to peer GPU copy
   * @retval FALSE if the pipeline id has peer to peer GPU copy
   */
  bool hasPeerToPeerCopy(size_t pipelineId) { return !requiresCopy((size_t)cudaId); }

  /**
   * Will automatically copy from one GPU to another (if it is required).
   *
   * Will check if the data being copied requires to be copied first, and then execute cudaMemcpyPeerAsync
   * if the data requires to be copied.
   *   
   * @param destination cuda memory that can be copied into, must be a pointer
   * @param data the source MemoryData that is allocated using a CudaMemoryManager (created using taskGraph->addCudaMemoryEdge)
   * @param numElems the number of elements to be copied
   * @return Whether the copy occurred or not
   * @retval TRUE if the copy was needed
   * @retval FALSE if the copy was not needed
   * @tparam V a type of MemoryData that is allocated using a CudaMemoryManager (created using taskGraph->addCudaMemoryEdge)
   * AND must be a pointer
   */
  template<class V>
  bool autoCopy(V *destination, std::shared_ptr<MemoryData<V>> data, long numElems) {

    if (requiresCopy(data)) {
      cudaMemcpyPeerAsync((void *) destination,
                          this->cudaId,
                          (void *) data->get(),
                          this->cudaIds[data->getPipelineId()],
                          sizeof(V) * numElems,
                          this->stream);
      return true;
    } else {
      return false;
    }
  }

  /**
   * Initializes the CudaTask to be bound to a particular GPU
   * @note This function should only be called by the HTGS API
   */
  void initialize() override final {
    this->cudaId = this->cudaIds[this->getPipelineId()];

    int numGpus;
    cudaGetDeviceCount(&numGpus);

    HTGS_ASSERT(this->cudaId < numGpus, "Error: Cuda ID: " << std::to_string(this->cudaId) << " is larger than the number of GPUs: " << std::to_string(numGpus));

    cudaSetDevice(this->cudaId);
    cudaStreamCreate(&stream);

    if (autoEnablePeerAccess) {

      for (size_t i = 0; i < this->numGpus; i++) {
        int peerId = this->cudaIds[i];
        if (peerId != this->cudaId) {
          int canAccess;
          cudaDeviceCanAccessPeer(&canAccess, this->cudaId, peerId);

          if (canAccess) {
            cudaDeviceEnablePeerAccess(peerId, 0);
          } else {
            this->nonPeerDevIds.push_back(peerId);
          }
        }
      }
    }

    this->initializeCudaGPU();
  }

  /**
   * Shutsdown the ICudaTask
   * @note This function should only be called by the HTGS API
   */
  void shutdown() override final {
    this->shutdownCuda();
    cudaStreamDestroy(stream);
  }

  /**
   * Gets the CUDA stream for this CUDA task
   * @return the CUDA stream
   */
  const cudaStream_t &getStream() const {
    return stream;
  }

  /**
   * Gets the cudaIds specified during ICudaTask construction
   * @return the cudaIds
   */
  int *getCudaIds() {
    return this->cudaIds;
  }

  /**
   * Gets the number of GPUs specified during ICudaTask construction
   * @return the number of GPUs
   */
  size_t getNumGPUs() {
    return this->numGpus;
  }

  /**
   * Synchronizes the Cuda stream associated with this task.
   *
   * @note Should only be called after initialization
   */
  void syncStream() {
    cudaStreamSynchronize(stream);
  }

 private:
  cudaStream_t stream; //!< The CUDA stream for the ICudaTask (set after initialize)
  int *cudaIds; //!< The array of cuda Ids (one per GPU)

  size_t numGpus; //!< The number of GPUs
  int cudaId; //!< The CudaID for the ICudaTask (set after initialize)
  std::vector<int> nonPeerDevIds; //!< The list of CudaIds that do not have peer-to-peer access
  bool autoEnablePeerAccess; //!< Flag to automatically enables peer access between multiple GPUs
};

}
#endif //HTGS_CUDATASK_HPP
#endif //USE_CUDA


