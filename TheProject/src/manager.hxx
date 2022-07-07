#pragma once

#include <span>

#include "logging.hxx"
#include "models/Model_DedicatedThreads.hxx"
#include "models/Model_MasterWorkerContract.hxx"
#include "models/Model_OurPipelining.hxx"

class Manager {
public:
	void StartUp() {}
	void Run(const uint32_t render_ops = 0, const uint32_t postprocess_ops = 0, const uint32_t physics_ops = 0
			,const uint32_t signalling_ops = 0, const uint32_t miscellaneous_tasks = 0) {
		LOG_info (__FUNCSIG__);
		++m_RunNumber;

		for (uint32_t pool_size: {5, 7, 9, 11}) {
			LOG_trace("Fpo Pool Size: {:d}", pool_size);
			for (ModelInterface* model: models) {
				for (auto& a_thread: worker_threads)
					if (a_thread.joinable ())
						a_thread.join ();
				model->SetSimulation (render_ops, postprocess_ops, physics_ops, signalling_ops, miscellaneous_tasks, pool_size);
				model->Run (m_RunNumber);
			}
		}
	};
private:
	std::thread worker_threads[15];
	uint32_t m_RunNumber = 0;
	Model_DedicatedThreads     a{std::span (worker_threads, 15)};// dedicated threads for Main-Workloads
	Model_MasterWorkerContract b{std::span (worker_threads, 15)}; // master thread creates tasks, worker threads complete them
	Model_OurPipelining        c{std::span (worker_threads, 15)}; // Our (Mine) method

	ModelInterface* models[2] {/*&a,*/ &b, &c};
};
// 15 10 20 32 17
