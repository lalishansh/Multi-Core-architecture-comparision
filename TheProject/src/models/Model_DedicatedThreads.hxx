#pragma once
#include <fstream>
#include <queue>

#include "ModelInterface.hxx"
#undef max
#undef min


class Model_DedicatedThreads: public ModelInterface
{
	static const uintptr_t CLOSE_SIGNAL = std::numeric_limits<uintptr_t>::max ();
	static const uint32_t NUM_OF_DEDICATED_THREADS = 4;

	Stats m_SignalAndScriptsStats;
	void signal_and_scripts() { // (Speciality: Commander), Producer for Operatable blocks, Issues some of Ope-ratable blocks to Miscellaneous and others to Physics
		m_SignalAndScriptsStats.ReStart ();
		const uint32_t NumOfOperations = m_Operations.signalling;

		m_SignalAndScriptsStats.Set (Stats::WAITING);

		for (uint32_t i = 0; i < std::max(NumOfFrameCount, NumOfMiscellaneousTasks); i++) {
			if (i < NumOfFrameCount) {
				ADummyOperatableBlock new_frame;

				m_SignalAndScriptsStats.Set (Stats::WORKING);

				// Perform operations
				for (uint32_t i = 0; i < NumOfOperations; i++)
					ADummyOperation (new_frame);

				m_SignalAndScriptsStats.Set (Stats::WAITING);

				physicsChannel.move_in (std::move (new_frame));
			}
			if (i < NumOfMiscellaneousTasks) {
				ADummyOperatableBlock miscellaneous_task;

				miscellaneousChannel.move_in (std::move (miscellaneous_task));
			}
		}

		ADummyOperatableBlock close_main((void*)CLOSE_SIGNAL);
		physicsChannel.move_in (std::move (close_main)); // it is upto the person on other side of channel what to do with it

		for (uint32_t i = 0; i < thread_pool.size() - NUM_OF_DEDICATED_THREADS; i++) {
			ADummyOperatableBlock close_miscellaneous((void*)CLOSE_SIGNAL);
			miscellaneousChannel.move_in (std::move (close_miscellaneous));
		}

		m_SignalAndScriptsStats.Stop ();
	}

	std::vector<Stats> m_MiscellaneousStats;
	Channel <ADummyOperatableBlock> miscellaneousChannel;
	void miscellaneous (const uint_fast8_t thread_idx) { // (Speciality: multiple siblings), Consumer for Operatable blocks
		m_MiscellaneousStats[thread_idx].ReStart ();
		const uint32_t NumOfOperations = m_Operations.miscellaneous;

		m_MiscellaneousStats[thread_idx].Set (Stats::WAITING);

		while(true) {
			auto task = miscellaneousChannel.move_out ();

			if (uintptr_t(task.Data ()) == CLOSE_SIGNAL)
				break;

			m_MiscellaneousStats[thread_idx].Set (Stats::WORKING);
			
			// perform task
			for (uint32_t i = 0; i < NumOfOperations; i++)
				ADummyOperation (task);

			m_MiscellaneousStats[thread_idx].Set (Stats::WAITING);

			// end task
			completedTasksTime.push_back (task.TimeSinceSpawn ());
		}

		m_MiscellaneousStats[thread_idx].Stop ();
	}

	Stats m_PhysicsStats;
	Channel <ADummyOperatableBlock> physicsChannel;
	void physics() { // (Speciality: Normie), Passes on Operatable blocks (after operating on it) to Render
		m_PhysicsStats.ReStart ();
		const uint32_t NumOfOperations = m_Operations.physics;

		m_PhysicsStats.Set (Stats::WAITING);

		while(true) {
			auto frame = physicsChannel.move_out ();
			
			if (uintptr_t(frame.Data ()) == CLOSE_SIGNAL)
				break;

			m_PhysicsStats.Set (Stats::WORKING);

			// perform operations
			for (uint32_t i = 0; i < NumOfOperations; i++)
				ADummyOperation (frame);

			m_PhysicsStats.Set (Stats::WAITING);

			renderQueueChannel.move_in (std::move (frame));
		}
		
		ADummyOperatableBlock close_render((void*)CLOSE_SIGNAL);
		renderQueueChannel.move_in (std::move (close_render));

		m_PhysicsStats.Stop ();
	}

	Stats m_RenderStats;
	Channel <ADummyOperatableBlock> renderQueueChannel;
	void render() { // (Speciality: Normie), Passes on Operatable blocks (after operating on it) to post_process
		m_RenderStats.ReStart ();
		const uint32_t NumOfOperations = m_Operations.render;

		m_RenderStats.Set (Stats::WAITING);

		while(true) {
			auto frame = renderQueueChannel.move_out ();

			if (uintptr_t(frame.Data ()) == CLOSE_SIGNAL)
				break;

			m_RenderStats.Set (Stats::WORKING);

			// perform operations
			for (uint32_t i = 0; i < NumOfOperations; i++)
				ADummyOperation (frame);

			m_RenderStats.Set (Stats::WAITING);

			postProcessChannel.move_in (std::move (frame));
		}

		ADummyOperatableBlock close_postprocess((void*)CLOSE_SIGNAL);
		postProcessChannel.move_in (std::move (close_postprocess));

		m_RenderStats.Stop ();
	}

	Stats m_PostprocessStats;
	Channel <ADummyOperatableBlock> postProcessChannel;
	void post_process() { // (Speciality: Normie), Consumer of Operatable blocks
		m_PostprocessStats.ReStart ();
		const uint32_t NumOfOperations = m_Operations.postprocess;

		m_PostprocessStats.Set (Stats::WAITING);
		while(true) {
			auto frame = postProcessChannel.move_out ();

			if (uintptr_t(frame.Data ()) == CLOSE_SIGNAL)
				break;

			m_PostprocessStats.Set (Stats::WORKING);

			// perform operations
			for (uint32_t i = 0; i < NumOfOperations; i++)
				ADummyOperation (frame);

			m_PostprocessStats.Set (Stats::WAITING);

			// end pipeline
			pipelineOutTime.push_back (frame.TimeSinceSpawn ());
		}
		m_PostprocessStats.Stop ();
	}

	std::vector <clock_t> pipelineOutTime;
	std::vector <clock_t> completedTasksTime;
	virtual void Run (const uint32_t run_number) override {
		LOG_trace (__FUNCTION__);

		pipelineOutTime.resize (NumOfFrameCount);
		completedTasksTime.resize (NumOfMiscellaneousTasks);
		pipelineOutTime.clear();
		completedTasksTime.clear();

		clock_t timer = clock();

		// dedicated
		thread_pool[0] = std::thread(BIND_FUNCTION(signal_and_scripts));
		thread_pool[1] = std::thread(BIND_FUNCTION(physics));
		thread_pool[2] = std::thread(BIND_FUNCTION(render));
		thread_pool[3] = std::thread(BIND_FUNCTION(post_process));

		const uint32_t NumMiscellaneousThreads = thread_pool.size() - NUM_OF_DEDICATED_THREADS;
		m_MiscellaneousStats.resize (NumMiscellaneousThreads);
		for (int i = 0; i < NumMiscellaneousThreads; i++) {
			thread_pool[NUM_OF_DEDICATED_THREADS + i] = std::thread(BIND_FUNCTION(miscellaneous), i);
		}

		// trace
		LOG_trace("\n");
		while (pipelineOutTime.size () != NumOfFrameCount || completedTasksTime.size () != NumOfMiscellaneousTasks) {
			LOG_raw("\r Frames: {} of {}, Tasks: {} of {};", pipelineOutTime.size (), NumOfFrameCount, completedTasksTime.size (), NumOfMiscellaneousTasks);
			std::this_thread::sleep_for (std::chrono::milliseconds(50));
		}
		LOG_raw("\r Frames: {}, Tasks: {};                             ", NumOfFrameCount, NumOfMiscellaneousTasks);

		for (auto &thred: thread_pool)
			if (thred.joinable ())
				thred.join ();

		constexpr auto ClocksPerSec = double(CLOCKS_PER_SEC);
		// display results
		std::ofstream log_file (fmt::format(PROJECT_ROOT_LOCATION "log/model_dedicatedThreads_run{}_threads{}.txt", run_number, thread_pool.size ()));
		log_file << fmt::format ("Last Run Completed in {} seconds.\n", double(clock() - timer)/ClocksPerSec);
		log_file << fmt::format("Script And Signalling [{:d} operations]:\n", m_Operations.signalling);
		log_file << '\t' << m_SignalAndScriptsStats.ToString () << '\n';
		log_file << fmt::format("Miscellaneous Tasks [{:d} operations]:\n", m_Operations.miscellaneous);
		for (int i = 0; i < NumMiscellaneousThreads; i++) 
			log_file << "\t#" << i << '\t' << m_MiscellaneousStats[i].ToString () << '\n';
		log_file << fmt::format("Physics [{:d} operations]:\n", m_Operations.physics);
		log_file << '\t' << m_PhysicsStats.ToString () << '\n';
		log_file << fmt::format("Render [{:d} operations]:\n", m_Operations.render);
		log_file << '\t' << m_RenderStats.ToString () << '\n';
		log_file << fmt::format("PostProcess [{:d} operations]:\n", m_Operations.postprocess);
		log_file << '\t' << m_PostprocessStats.ToString () << '\n';

		log_file << "\nFrame Stats";
		for (uint32_t i = 0; i < pipelineOutTime.size (); i++)
			log_file << fmt::format("\n\t  #{:d}\tTimeTaken: {}ms", i + 1, pipelineOutTime[i]);
		
		log_file << "\nMiscellaneous Tasks Stats" << '\n';
		for (uint32_t i = 0; i < completedTasksTime.size (); i++)
			log_file << fmt::format("\n\t  #{:d}\tTimeTaken: {}ms", i + 1, completedTasksTime[i]);

		log_file.close();
	}
public:
	Model_DedicatedThreads (std::span<std::thread> in) :ModelInterface (in) {}
};