#pragma once
#include "ModelInterface.hxx"

class Model_OurPipelining: public ModelInterface
{
	static const uintptr_t CLOSE_SIGNAL = std::numeric_limits<uintptr_t>::max (); // close contract

	// Idea is to devide into smaller chunks then pipelining it
	enum PipelineStage: uint32_t {
		None = 0,
		SIGNALLING,
		PHYSICS,
		RENDER,
		POSTPROCESS
		// MISCELLANEOUS // not a stage but a sudden task
	};

	struct Addr {
		uint32_t stage;
		uint32_t idx;
	};
#define START_STAGE(stage)			case stage: { constexpr uint32_t this_stage = stage;
#define END_STAGE(stage)			if (stage == this_stage) break; }

	void pipeline (ADummyOperatableBlock &frame, const Addr from, const Addr till) {
		auto get_bounds = [](const uint32_t this_stage, const uint32_t min, const uint32_t max, const Addr from, const Addr till) -> std::pair<uint32_t, uint32_t> 
								{ return {(this_stage == from.stage ? from.idx : min), (this_stage == till.stage ? till.idx : max)}; };
		
		frame.AddToStack ("entered_pipeline");
		switch (from.stage) {
			START_STAGE (PipelineStage::SIGNALLING)
				frame.AddToStack ("stage_signalling");
				const auto [start, end] = get_bounds (this_stage, 0, m_Operations.signalling, from, till);
				for (uint32_t i = start; i < end; i++) {
					ADummyOperation (frame);
				}
			END_STAGE(till.stage)
			START_STAGE (PipelineStage::PHYSICS)
				frame.AddToStack ("stage_physics");
				const auto [start, end] = get_bounds (this_stage, 0, m_Operations.physics, from, till);
				for (uint32_t i = start; i < end; i++) {
					ADummyOperation (frame);
				}
			END_STAGE(till.stage)
			START_STAGE (PipelineStage::RENDER)
				frame.AddToStack ("stage_render");
				const auto [start, end] = get_bounds (this_stage, 0, m_Operations.render, from, till);
				for (uint32_t i = start; i < end; i++) {
					ADummyOperation (frame);
				}
			END_STAGE(till.stage)
			START_STAGE (PipelineStage::POSTPROCESS)
				frame.AddToStack ("stage_postprocess");
				const auto [start, end] = get_bounds (this_stage, 0, m_Operations.postprocess, from, till);
				for (uint32_t i = start; i < end; i++) {
					ADummyOperation (frame);
				}
			END_STAGE(till.stage)
		};
		frame.AddToStack ("exiting_pipeline");
	};

	std::vector <clock_t>                       pipelineOutTime;
	std::vector <std::vector<std::string_view>> pipelineOutStack;
	std::vector <clock_t>                       completedTasksTime;
	std::vector <std::vector<std::string_view>> completedTasksStack;
	virtual void Run(const uint32_t run_number) override {
		LOG_trace (__FUNCTION__);

		struct Handle {
			union {
				bool is_available;
				bool is_occupied;
			};
			ADummyOperatableBlock block;
			Handle () {
				is_available = false;
			}
		};
		const uint32_t num_of_main_threads = uint32_t (thread_pool.size ()*0.85f);
		// Note: Although Handle is not thread safe, it dosen't matter as only one thread will try to write into a handle and only one will try to acquire it
		std::vector<Handle> frame_rail (num_of_main_threads-1); // of num_threads size - 1, every new frame will be extracted from producer, every thread will promote it till the last one where it will be popped out
		// Here since the 0th thread will extract frame from frame channel
		// Assigning 1/5 thread for miscelleneous tasks
		Channel<ADummyOperatableBlock> miscelleneous_task_channel;
		std::mutex miscelleneous_sync_mutex;
		auto task_thread = [this](Channel<ADummyOperatableBlock>* task_channel, std::vector<clock_t>* completed_tasks_time, std::vector<std::vector<std::string_view>>* completed_tasks_stack, Stats* stats, std::mutex* sync_mutex) {
			stats->ReStart ();
			while (true) {
				auto task = task_channel->move_out ();

				if (uintptr_t(task.Data ()) == CLOSE_SIGNAL)
					break;

				stats->Set (Stats::WORKING);
			
				// perform task
				for (uint32_t i = 0; i < m_Operations.miscellaneous; i++)
					ADummyOperation (task);

				stats->Set (Stats::WAITING);

				// end task
				std::unique_lock lock (*sync_mutex);
				completed_tasks_time->push_back (task.TimeSinceSpawn ());
				completed_tasks_stack->push_back (std::move (task.GetStack_raw ()));
			}
			stats->Stop ();
		};
		auto first_main_thread = [this](Handle* out_handle, const Addr from, const Addr till, Channel<ADummyOperatableBlock>* task_channel, Stats* stats, const uint32_t num_of_task_thread) {
			stats->ReStart ();
			stats->Set (Stats::WAITING);

			uint32_t number_of_frames_left_to_process = NumOfFrameCount;
			uint32_t number_of_miscellaneous_tasks_left = NumOfMiscellaneousTasks;
			uint32_t schedule_task_counter = 0;
			bool keep_running;
			do {
				keep_running = number_of_frames_left_to_process != 0 || number_of_miscellaneous_tasks_left != 0;
				schedule_task_counter++;

				bool new_frame_prepared = keep_running ? false:true; // last close signal frame
				ADummyOperatableBlock new_frame ((void*)(keep_running ? 0 : CLOSE_SIGNAL));

				if (number_of_frames_left_to_process != 0) {
					uint32_t data[2] {NumOfMiscellaneousTasks - number_of_miscellaneous_tasks_left, schedule_task_counter - 1};
					ADummyOperatableBlock new_task ((void*)(*(uint64_t*) (&data)));
					new_task.AddToStack ("created");

					new_frame = std::move (new_task);
					number_of_frames_left_to_process--;

					stats->Set (Stats::WORKING);
					pipeline (new_frame, from, till); // PIPELINE WORK
					stats->Set (Stats::WAITING);

					new_frame_prepared = true;
				}
				if (number_of_miscellaneous_tasks_left != 0) {
					uint32_t data[2] {NumOfMiscellaneousTasks - number_of_miscellaneous_tasks_left, schedule_task_counter - 1};
					ADummyOperatableBlock new_task ((void*)(*(uint64_t*) (&data)));
					new_task.AddToStack ("created");

					number_of_miscellaneous_tasks_left--;
					new_task.AddToStack ("moving_to_channel");
					task_channel->move_in (std::move (new_task));

					if (number_of_miscellaneous_tasks_left == 0) {
						for (uint32_t i = 0; i < num_of_task_thread; i++) {
							ADummyOperatableBlock close_signal ((void*)(CLOSE_SIGNAL)); 
							task_channel->move_in (std::move (close_signal));
						}
					}
				} 

				if (new_frame_prepared) {
					new_frame.AddToStack ("handing_over_to_mid_thread");
					while (out_handle->is_occupied) std::this_thread::sleep_for (std::chrono::milliseconds(1));
					out_handle->block = std::move (new_frame);
					out_handle->is_available = true;
				}
			} while (keep_running);

			stats->Stop ();
		};
		auto mid_main_thread = [this](Handle* out_handle, Handle* in_handle, const Addr from, const Addr till, Stats* stats) {
			stats->ReStart ();
			stats->Set (Stats::WAITING);
			while (true) {
				while (!in_handle->is_available) std::this_thread::sleep_for (std::chrono::milliseconds(1));
				auto frame = std::move (in_handle->block);
				in_handle->is_occupied = false;
				frame.AddToStack ("extracted_by_mid_thread");

				bool close_signal = uintptr_t (frame.Data ()) == CLOSE_SIGNAL;
				if (!close_signal) {
					stats->Set (Stats::WORKING);
					pipeline (frame, from, till);
					stats->Set (Stats::WAITING);
				}

				
				frame.AddToStack ("handing_over_to_next_thread");
				while (out_handle->is_occupied) std::this_thread::sleep_for (std::chrono::milliseconds(1));
				out_handle->block = std::move (frame);
				out_handle->is_available = true;

				if (close_signal) break;
			};

			stats->Stop ();
		};
		auto last_main_thread = [this](Handle* in_handle, const Addr from, const Addr till, Stats* stats, std::vector<clock_t> *pipeline_out_time, std::vector<std::vector<std::string_view>> *pipeline_out_stack) {
			stats->ReStart ();
			stats->Set (Stats::WAITING);
			while (true) {
				while (!in_handle->is_available) std::this_thread::sleep_for (std::chrono::milliseconds(1));
				auto frame = std::move (in_handle->block);
				in_handle->is_occupied = false;
				frame.AddToStack ("extracted_by_last_thread");

				bool close_signal = uintptr_t (frame.Data ()) == CLOSE_SIGNAL;
				if (!close_signal) {
					stats->Set (Stats::WORKING);
					pipeline (frame, from, till);
					stats->Set (Stats::WAITING);
				}

				// log
				pipeline_out_time->push_back (frame.TimeSinceSpawn ());
				pipeline_out_stack->push_back (std::move (frame.GetStack_raw ()));

				frame.AddToStack ("getting_destroyed_by_last_thread");
				if (close_signal) break;
			};

			stats->Stop ();
		};
		
		std::vector<Stats> stats (thread_pool.size ());
		std::vector<std::pair<Addr, Addr>> from_tills (num_of_main_threads);
		{
			const double total_work = m_Operations.signalling + m_Operations.physics + m_Operations.render + m_Operations.postprocess;
			std::vector<double> thread_work (num_of_main_threads);
			std::vector<uint32_t> u_thread_work (num_of_main_threads);
			for (auto& stage_work: thread_work)
				stage_work = total_work/num_of_main_threads;

			for (uint32_t i = 0; i < num_of_main_threads-1; i++) {
				double delta = thread_work[i] - std::floor (thread_work[i]);
				if (delta > 0.5) {
					u_thread_work[i] = uint32_t (thread_work[i]) + 1;
					thread_work[i + 1] -= (1.0 - delta);
				} else {
					u_thread_work[i] = uint32_t (thread_work[i]);
					thread_work[i + 1] += delta;
				}
			} { // last
				u_thread_work[num_of_main_threads-1] = uint32_t(thread_work.back ()); + (thread_work.back () - std::floor (thread_work.back ()) > 0.5 ? 0 : 1);
				// due to msvc floating point model, floating conversions are always slightly higher anyways
			}

			const uint32_t u_stages_work[4] = {m_Operations.signalling , m_Operations.physics , m_Operations.render , m_Operations.postprocess};
			uint32_t store = 0;
			for (uint32_t i = 0; i < num_of_main_threads; i++) {
				auto f = store;
				auto t = store + u_thread_work[i];
				uint32_t f_stg = 0;
				uint32_t t_stg = 0;
				for (auto stage: u_stages_work)
					if (f > stage)
						f-=stage, ++f_stg;
					else break;
				for (auto stage: u_stages_work)
					if (t > stage)
						t-=stage, ++t_stg;
					else break;

				from_tills[i].first.stage = f_stg + PipelineStage::SIGNALLING;
				from_tills[i].second.stage = t_stg + PipelineStage::SIGNALLING;
				from_tills[i].first.idx = f;
				from_tills[i].second.idx = t;

				store += u_thread_work[i];
			}
		}

		pipelineOutTime.clear ();
		pipelineOutTime.reserve (NumOfFrameCount);
		pipelineOutStack.clear ();
		pipelineOutStack.reserve (NumOfFrameCount);
		completedTasksTime.clear ();
		completedTasksTime.reserve (NumOfMiscellaneousTasks);
		completedTasksStack.clear ();
		completedTasksStack.reserve (NumOfMiscellaneousTasks);

		thread_pool[0] = std::thread (first_main_thread, &frame_rail.front (), from_tills[0].first, from_tills[0].second, &miscelleneous_task_channel, &stats[0], thread_pool.size ()-num_of_main_threads);
		for (uint32_t i = 1; i < num_of_main_threads - 1; i++)
			thread_pool[i] = std::thread (mid_main_thread, &frame_rail[i], &frame_rail[i-1], from_tills[i].first, from_tills[i].second, &stats[i]);
		thread_pool[num_of_main_threads - 1] = std::thread (last_main_thread, &frame_rail.back (), from_tills.back ().first, from_tills.back ().second, &stats[num_of_main_threads - 1], &pipelineOutTime, &pipelineOutStack);
		
		for (uint32_t i = num_of_main_threads; i < thread_pool.size (); i++) 
			thread_pool[i] = std::thread (task_thread, &miscelleneous_task_channel, &completedTasksTime, &completedTasksStack, &stats[i], &miscelleneous_sync_mutex);

		// track
		LOG_trace("\n");
		while (pipelineOutTime.size () < NumOfFrameCount || completedTasksTime.size () < NumOfMiscellaneousTasks) {
			LOG_raw("\r Frames: {} of {}, Tasks: {} of {};", pipelineOutTime.size (), NumOfFrameCount, completedTasksTime.size (), NumOfMiscellaneousTasks);
			std::this_thread::sleep_for (std::chrono::milliseconds(50));
		}
		LOG_raw("\r Frames: {}, Tasks: {};                             ", NumOfFrameCount, NumOfMiscellaneousTasks);

		for (auto &thred: thread_pool)
			if (thred.joinable ())
				thred.join ();

		std::ofstream log_file (fmt::format(PROJECT_ROOT_LOCATION "log/model_ourPipeline_run{}_threads{}.txt", run_number, thread_pool.size ()));
		log_file << fmt::format("Contracted threads:\n");
		for (int i = 0; i < num_of_main_threads; i++) 
			log_file << "\t#" << i << '\t' << stats[i].ToString () << '\n';

		log_file << fmt::format("Miscellaneous Tasks threads:\n");
		for (int i = num_of_main_threads; i < thread_pool.size (); i++) 
			log_file << "\t#" << i << '\t' << stats[i].ToString () << '\n';
		
		log_file << "\nFrame Stats";
		for (uint32_t i = 0; i < pipelineOutTime.size (); i++) {
			log_file << fmt::format("\n\t  #{:d}\tTimeTaken: {}ms \t STACK: \t", i + 1, pipelineOutTime[i]);
			for (auto str: pipelineOutStack[i]) 
				log_file << " >> " << str;
		}
		log_file << "\nMiscellaneous Tasks Stats" << '\n';
		for (uint32_t i = 0; i < completedTasksTime.size (); i++) {
			log_file << fmt::format("\n\t  #{:d}\tTimeTaken: {}ms \t STACK: \t", i + 1, completedTasksTime[i]);
			for (auto str: completedTasksStack[i]) 
				log_file << " >> " << str;
		}

	}
public:
	Model_OurPipelining (std::span<std::thread> in) :ModelInterface (in) {}
};