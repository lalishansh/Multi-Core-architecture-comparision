#pragma once
#include "ModelInterface.hxx"

class Model_MasterWorkerContract: public ModelInterface
{
	static const uintptr_t CLOSE_SIGNAL = std::numeric_limits<uintptr_t>::max (); // close contract

	template<typename Task, typename Outcome>
	struct TaskManager {
		using token = uint32_t;
	public:
		bool RoomForNewTasks () { return !m_PendingTasks.full (); }
		bool Resize(const uint16_t NumberOfTaskRunners, const uint32_t NumberOfBuffering) { // single double, triple buffering etc.
			std::unique_lock <std::mutex> lock (m_p);

			while (m_WindowHead != m_PendingTasks.head() || m_PendingTasks.size () > 0) // wait till no task pending, and completed tasks dumped
				c_p.wait (lock);

			// all issued tasks are completed
			const uint32_t new_capacity = (NumberOfBuffering + 1)*NumberOfTaskRunners;

			if (m_CompletedTasks != nullptr)
				delete m_CompletedTasks; // free (m_CompletedTasks);
			m_CompletedTasks = nullptr;

			m_CompletedTasks = new Outcome[new_capacity];// (Outcome*)(malloc (sizeof (Outcome)*new_capacity));
			m_PendingTasks.resize_capacity (new_capacity); // head set to zero
			m_CompletedTasksFlagging.resize (new_capacity);

			m_TotalCapacity = new_capacity;
			m_WindowMaxSize = new_capacity - NumberOfTaskRunners;
			m_WindowHead = 0;
			
			c_p.notify_one ();
			return true;
		}
		void SetDumpFunction (void (*dump_func)(Outcome&&, void*), void* user_extra = nullptr) {
			std::unique_lock <std::mutex> lock (m_o);
			dump_completed_tasks_here = dump_func;
			m_UserExtra = user_extra;
		}
		void SubmitNewTask_thread_lock (Task &&task) {
			task.AddToStack (__FUNCTION__);
			assert (m_TotalCapacity != 0);
			assert (dump_completed_tasks_here != nullptr);

			std::unique_lock <std::mutex> lock (m_p);
			while (m_PendingTasks.full ())
				c_p.wait (lock); // release lock as long as the wait and reaquire it afterwards.

			std::unique_lock <std::mutex> lock_guard (m_c);
			m_PendingTasks.unsafe_move_in (std::move (task));
			c_c.notify_one ();
		}
		token GetTask_thread_locked (Task &&task) {
			std::unique_lock <std::mutex> lock (m_c);
			while (m_PendingTasks.empty ())
				c_c.wait (lock); // release lock as long as the wait and reaquire it afterwards.

			token x =  m_PendingTasks.unsafe_move_out (std::move (task));
			task.AddToStack (__FUNCTION__);

			c_p.notify_one ();
			c_c.notify_one ();
			return x;
		}
		void SubmitOutcome_thread_locked (Outcome &&result, token tkn) {
			result.AddToStack (__FUNCTION__);

			while (true) {
				uint32_t x = tkn + (tkn < m_WindowHead ? m_TotalCapacity:0) - m_WindowHead;
				if(x < m_WindowMaxSize)
					break;
				std::this_thread::sleep_for (std::chrono::milliseconds(1));
			}
			std::unique_lock <std::mutex> lock (m_o);

			m_CompletedTasks[tkn] = std::move(result);
			m_CompletedTasksFlagging[tkn] = true;
			while (m_CompletedTasksFlagging[m_WindowHead]) {
				m_CompletedTasks[m_WindowHead].AddToStack ("GettingDumpedTo_dump_func");
				dump_completed_tasks_here (std::move (m_CompletedTasks[m_WindowHead]), m_UserExtra);
				m_CompletedTasksFlagging[m_WindowHead] = false;

				m_WindowHead++;
				if (m_WindowHead + 1 > m_TotalCapacity)
					m_WindowHead -= m_TotalCapacity;
			};
		}
		~TaskManager() {
			//if (m_CompletedTasks != nullptr)
			//	delete m_CompletedTasks; // free (m_CompletedTasks);
			m_CompletedTasks = nullptr;
		}
	private:
		template<typename Typ>
		struct CQ { // Unidirn Circular Queue
			bool resize_capacity (const uint32_t new_size) {
				if (new_size <= m_Size)
					return false;
				if (new_size == m_Capacity)
					return true;

				Typ* arr = new Typ[new_size];// (Typ*)(malloc (sizeof (Typ)*new_size));

				if (m_Arr != nullptr) {
					for (uint32_t i = 0; i < m_Size; i++) {
						uint32_t j = i + m_Head;
						if (j > m_Capacity)
							j -= m_Capacity;
						arr[i] = std::move (m_Arr[j]);
					}

					delete m_Arr; // free (m_Arr);
					m_Arr = nullptr;
				}

				m_Arr = arr;
				m_Capacity = new_size;
				m_Head = 0;

				return true;
			}

			FORCEINLINE uint32_t move_in(Typ &&in) { // returns location of push, 
				if (full())
					return uint32_t (-1);
 
				return unsafe_move_in (std::move (in));
			}
			FORCEINLINE uint32_t move_out(Typ& in) {
				if (m_Size == 0)
					return uint32_t (-1);

				return unsafe_move_out (in);
			}
			FORCEINLINE uint32_t unsafe_move_in(Typ&& in) { // returns location of push, 
				uint32_t x = m_Head + m_Size++; // x ∈ [0, 2*m_Capacity), for replacing (x % m_Capacity)
				x -= (x + 1 > m_Capacity ? m_Capacity : 0);
				m_Arr [x] = std::move (in); 
				return x;
			}
			FORCEINLINE uint32_t unsafe_move_out(Typ&& in) {
				uint32_t x = m_Head++; m_Size--;
				m_Head -= (m_Head + 1 > m_Capacity ? m_Capacity : 0);
				in = std::move (m_Arr [x]);
				return x;
			}
			FORCEINLINE const bool full () const {
				return m_Size + 1 > m_Capacity;
			}
			FORCEINLINE const bool empty () const {
				return m_Size == 0;
			}
			FORCEINLINE const uint32_t size () const {
				return m_Size;
			}
			FORCEINLINE const uint32_t head () const {
				return m_Head;
			}

			~CQ() {
				//if (m_Arr != nullptr)
				//	delete m_Arr; // free (m_Arr);
				m_Arr = nullptr;
			}
		private:
			Typ* m_Arr = nullptr;
			uint32_t m_Head = 0, m_Size = 0;
			uint32_t m_Capacity = 0;
		};
		CQ<Task> m_PendingTasks; // CQ of (N + 1) * numTaskRunners

		std::vector<bool> m_CompletedTasksFlagging;
		Outcome* m_CompletedTasks = nullptr; // ARR of (N + 1) * numTaskRunners
		uint32_t m_TotalCapacity = 0; // (N + 1) * numTaskRunners
		uint32_t m_WindowMaxSize = 0; // (N) * numTaskRunners
		uint32_t m_WindowHead = 0; // (N) * numTaskRunners

		void (*dump_completed_tasks_here)(Outcome&&, void*) = nullptr;
		void* m_UserExtra = nullptr;
			
		std::mutex              m_c, m_p, m_o;
		std::condition_variable c_c, c_p;
	};

	template<typename Typ>
	class TheTask {
		Typ block = Typ();
		void* extra = nullptr;
		void (*perform) (Typ& block, void*) = nullptr;
	public:
		TheTask (Typ &&block, void (*perform) (Typ& block, void*), void* extra = nullptr)
			: block (std::move (block)), perform (perform), extra(extra)
		{}
		TheTask () = default;
		
		void AddToStack (std::string_view func_name) {
			block.AddToStack (func_name);
		}

		Typ&& Run() {
			if (perform != nullptr)
				perform(block, extra);
			return std::move(block);
		}
	};

	Stats m_MasterThreadStats;
	TaskManager<TheTask<ADummyOperatableBlock>, ADummyOperatableBlock> *m_TaskManager;

	std::vector <clock_t> pipelineOutTime;
	std::vector <std::vector<std::string_view>> pipelineOutStack;
	std::vector <clock_t> completedTasksTime;
	std::vector <std::vector<std::string_view>> completedTasksStack;

	uint32_t m_NumberOfFramesPushed = 0;
	uint32_t m_NumberOfTasksPushed = 0;
	void MasterThread (const uint32_t num_of_contracts) {
		m_MasterThreadStats.ReStart ();

		using Task = TheTask<ADummyOperatableBlock>;
		using Outcome = ADummyOperatableBlock;

		static Channel<Outcome> dump;
		auto dump_func = [](Outcome&& in, void* dump_ptr) {
			auto &dump = *(Channel<Outcome>*)(dump_ptr);
			in.AddToStack ("Received_dump_func");

			dump.move_in (std::move (in));
		};
		m_TaskManager->SetDumpFunction (dump_func, (void*)(&dump));
		enum Stage: uint32_t {
			None = 0,
			SIGNALLING,
			PHYSICS,
			RENDER,
			POSTPROCESS,
			MISCELLANEOUS
		};

		auto signallingOperation = [] (ADummyOperatableBlock& frame, void* extra_data) {
			frame.AddToStack ("signallingOperation ()");
			const auto num_of_operations = uint32_t (extra_data);
			for (uint32_t i = 0; i < num_of_operations; i++) ADummyOperation (frame);
		};
		auto physicsOperation = [] (ADummyOperatableBlock& frame, void* extra_data) {
			frame.AddToStack ("physicsOperation ()");
			const auto num_of_operations = uint32_t (extra_data);
			for (uint32_t i = 0; i < num_of_operations; i++) ADummyOperation (frame);
		};
		auto renderOperation = [] (ADummyOperatableBlock& frame, void* extra_data) {
			frame.AddToStack ("renderOperation ()");
			const auto num_of_operations = uint32_t (extra_data);
			for (uint32_t i = 0; i < num_of_operations; i++) ADummyOperation (frame);
		};
		auto postprocessOperation = [] (ADummyOperatableBlock& frame, void* extra_data) {
			frame.AddToStack ("postprocessOperation ()");
			const auto num_of_operations = uint32_t (extra_data);
			for (uint32_t i = 0; i < num_of_operations; i++) ADummyOperation (frame);
		};
		auto miscellaneousOperation = [] (ADummyOperatableBlock& frame, void* extra_data) {
			frame.AddToStack ("miscellaneousOperation ()");
			const auto num_of_operations = uint32_t (extra_data);
			for (uint32_t i = 0; i < num_of_operations; i++) ADummyOperation (frame);
		};

		union TASK_Payload {
			struct {
				uint32_t frame_num, stage;
			};
			uintptr_t block;
		};

		uint32_t number_of_frames_left_to_process = NumOfFrameCount;
		uint32_t number_of_miscellaneous_tasks_left = NumOfMiscellaneousTasks;
		uint32_t num_of_contracts_left = num_of_contracts;
		uint32_t schedule_task_counter = 0;
		uint32_t completed_task_counter = 0;
		while (true) {
			TASK_Payload data;

			bool dumped_task_ready = false;
			while (dump.peek () != nullptr) {
				const auto* peekabo = dump.peek ();

				TASK_Payload data{.block = uintptr_t(peekabo->Data ())};

				if (data.stage == SIGNALLING || data.stage == PHYSICS || data.stage == RENDER) {
					// promote
					dumped_task_ready = true;
					break;
				}
				if (data.stage == MISCELLANEOUS) { // journey come to an end
					completedTasksTime.push_back (peekabo->TimeSinceSpawn ());
					completedTasksStack.push_back (std::move (peekabo->GetStack_raw ()));
				} else if (data.stage == POSTPROCESS) { // journey come to an end
					pipelineOutTime.push_back (peekabo->TimeSinceSpawn ());
					pipelineOutStack.push_back (std::move (peekabo->GetStack_raw ()));
				} else if (data.block == CLOSE_SIGNAL) {
					num_of_contracts_left--;
				} else // like None, Invalid number etc.
					THROW_critical ("why the hell is this coming from dump");

				Outcome out(nullptr);
				dump.try_move_out (std::move (out));
				completed_task_counter++;

				out.AddToStack ("extracted from dump");
				out.AddToStack (" & destroyed");
			}
			bool ready_to_send = false;
			if (m_TaskManager->RoomForNewTasks ()) {
				Outcome out;
				if (dumped_task_ready) { // dump is empty
					dump.try_move_out (std::move (out));
					// coming out of dump
					data.block = uintptr_t (out.Data ());
					//if (data.stage == SIGNALLING || data.stage == PHYSICS || data.stage == RENDER) { // promote
						data.stage += 1;
					//} 
					out.AddToStack ("extracted from dump");
					out.AddToStack (" & gettingPromoted");
					out.SetData ((void*)(data.block));
					ready_to_send = true;
				} else {
					ready_to_send = true;
					if (number_of_frames_left_to_process != 0 && number_of_miscellaneous_tasks_left != 0) {
						if (schedule_task_counter % 2 == 0) {
							data.frame_num = NumOfFrameCount - number_of_frames_left_to_process, data.stage = SIGNALLING;
							number_of_frames_left_to_process--;
						} else {
							data.frame_num = NumOfMiscellaneousTasks - number_of_miscellaneous_tasks_left, data.stage = MISCELLANEOUS;
							number_of_miscellaneous_tasks_left--;
						}
						schedule_task_counter++;
					} else if (number_of_frames_left_to_process == 0 && number_of_miscellaneous_tasks_left != 0) {
						data.frame_num = NumOfMiscellaneousTasks - number_of_miscellaneous_tasks_left;
						data.stage = MISCELLANEOUS;
						number_of_miscellaneous_tasks_left--;
						schedule_task_counter++;
					} else if (number_of_frames_left_to_process != 0 && number_of_miscellaneous_tasks_left == 0) {
						data.frame_num = NumOfFrameCount - number_of_frames_left_to_process;
						data.stage = SIGNALLING;
						number_of_frames_left_to_process--;
						schedule_task_counter++;
					} else // if (number_of_frames_left_to_process == 0 && number_of_miscellaneous_tasks_left == 0)
						if (schedule_task_counter == completed_task_counter) // 
							data.block = CLOSE_SIGNAL, schedule_task_counter++;
						else ready_to_send = false;
					out.AddToStack ("created");
					out.SetData ((void*)(data.block));
				};
				if (ready_to_send) {
					// Ready to send again
					switch (data.stage) {
						case SIGNALLING: {
							m_NumberOfFramesPushed++;
							out.AddToStack ("submittedFor signallingOperation");
							m_TaskManager->SubmitNewTask_thread_lock (TheTask <ADummyOperatableBlock> (std::move (out), signallingOperation, (void*) (m_Operations.signalling)));
							break;
						}
						case PHYSICS: {
							out.AddToStack ("submittedFor physicsOperation");
							m_TaskManager->SubmitNewTask_thread_lock (TheTask <ADummyOperatableBlock> (std::move (out), physicsOperation, (void*) (m_Operations.physics)));
							break;
						}
						case RENDER: {
							out.AddToStack ("submittedFor renderOperation");
							m_TaskManager->SubmitNewTask_thread_lock (TheTask <ADummyOperatableBlock> (std::move (out), renderOperation, (void*) (m_Operations.render)));
							break;
						}
						case POSTPROCESS: {
							out.AddToStack ("submittedFor postprocessOperation");
							m_TaskManager->SubmitNewTask_thread_lock (TheTask <ADummyOperatableBlock> (std::move (out), postprocessOperation, (void*) (m_Operations.postprocess)));
							break;
						}
						case MISCELLANEOUS: {
							m_NumberOfTasksPushed++;
							out.AddToStack ("submittedFor miscellaneousOperation");
							m_TaskManager->SubmitNewTask_thread_lock (TheTask <ADummyOperatableBlock> (std::move (out), miscellaneousOperation, (void*) (m_Operations.miscellaneous)));
							break;
						}
						default: {
							out.AddToStack ("submittedFor nothing");
							m_TaskManager->SubmitNewTask_thread_lock (TheTask <ADummyOperatableBlock> (std::move (out), nullptr));
							break;
						}
					};
				}
			}
			if (num_of_contracts_left == 0)
				break;
		}

		m_MasterThreadStats.Stop ();
	}

	std::vector<Stats> m_ContractedThreadsStats;
	void ContractedThread (const uint32_t thread_idx) { // or TaskRunner
		m_ContractedThreadsStats[thread_idx].ReStart ();

		m_ContractedThreadsStats[thread_idx].Set (Stats::WAITING);

		ADummyOperatableBlock outcome; // currently empty
		TheTask<ADummyOperatableBlock> task(std::move (outcome), nullptr, nullptr);
		while(true) {
			const auto token = m_TaskManager->GetTask_thread_locked (std::move(task));

			m_ContractedThreadsStats[thread_idx].Set (Stats::WORKING);
			outcome = task.Run ();
			m_ContractedThreadsStats[thread_idx].Set (Stats::WAITING);

			bool close_signal = uintptr_t (outcome.Data ()) == CLOSE_SIGNAL;

			m_TaskManager->SubmitOutcome_thread_locked (std::move (outcome), token);

			if (close_signal)
				break;
		}

		m_ContractedThreadsStats[thread_idx].Stop ();
	}

	virtual void Run(const uint32_t run_number) override {
		LOG_trace (__FUNCTION__);
		TaskManager<TheTask<ADummyOperatableBlock>, ADummyOperatableBlock> the_task_manager;
		m_TaskManager = &the_task_manager;

		const uint16_t num_of_contacted_threads = thread_pool.size() - 1;
		m_ContractedThreadsStats.resize (num_of_contacted_threads);
		pipelineOutTime.reserve (NumOfFrameCount);
		completedTasksTime.reserve (NumOfMiscellaneousTasks);
		pipelineOutStack.reserve (NumOfFrameCount);
		completedTasksStack.reserve (NumOfMiscellaneousTasks);
		m_NumberOfFramesPushed = 0;
		m_NumberOfTasksPushed = 0;

		m_TaskManager->Resize (num_of_contacted_threads, 3);
		thread_pool[0] = std::thread(BIND_FUNCTION(MasterThread), num_of_contacted_threads);
		for (uint32_t i = 0; i < num_of_contacted_threads; i++)
			thread_pool[i + 1] = std::thread(BIND_FUNCTION(ContractedThread), i);

		// track
		LOG_trace("\n");
		while (pipelineOutTime.size () != NumOfFrameCount || completedTasksTime.size () != NumOfMiscellaneousTasks) {
			LOG_raw("\r Frames: {} of {}, Tasks: {} of {};", pipelineOutTime.size (), NumOfFrameCount, completedTasksTime.size (), NumOfMiscellaneousTasks);
			std::this_thread::sleep_for (std::chrono::milliseconds(50));
		}
		LOG_raw("\r Frames: {}, Tasks: {};                             ", NumOfFrameCount, NumOfMiscellaneousTasks);

		for (auto &thred: thread_pool)
			if (thred.joinable ())
				thred.join ();

		// output
		std::ofstream log_file (fmt::format(PROJECT_ROOT_LOCATION "log/model_masterWorkerContract_run{}_threads{}.txt", run_number, thread_pool.size ()));
		log_file << fmt::format("Master thread: Total-time  {}sec\n", double (m_MasterThreadStats.Get_TotalTimeInSeconds ()));
		log_file << fmt::format("Contracted threads:\n");
		for (int i = 0; i < num_of_contacted_threads; i++) 
			log_file << "\t#" << i << '\t' << m_ContractedThreadsStats[i].ToString () << '\n';
		
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
	Model_MasterWorkerContract (std::span<std::thread> in) :ModelInterface (in) {}
};
