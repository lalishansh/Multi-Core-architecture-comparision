#pragma once
#include <cassert>
#include <atomic>
#include <queue>
#include <span>;

#include "common.hxx";
#undef max
#undef min

class ModelInterface {
public:
	static const uint32_t NumOfFrameCount = 1000;
	static const uint32_t NumOfMiscellaneousTasks = 1000;

	ModelInterface (std::span <std::thread> threads)
		:thread_pool_full (threads)
	{}

	void SetSimulation(const uint32_t render_ops, const uint32_t postprocess_ops, const uint32_t physics_ops
			,const uint32_t signalling_ops, const uint32_t miscellaneous_tasks, const uint32_t try_pool_size)
	{
		;m_Operations.render = render_ops, m_Operations.postprocess = postprocess_ops
		,m_Operations.physics = physics_ops, m_Operations.signalling = signalling_ops
		,m_Operations.miscellaneous = miscellaneous_tasks;

		thread_pool = std::span (thread_pool_full.data (), std::min (try_pool_size, uint32_t (thread_pool_full.size ())));
	}

	virtual void Run(const uint32_t run_number) {
		LOG_trace(__func__);
	}
protected:
	const std::span <std::thread> thread_pool_full;
	std::span <std::thread> thread_pool;
	struct {
		uint32_t render, postprocess, physics, signalling, miscellaneous;
	} m_Operations;

	struct Stats {
		enum StatusFlag: uint_fast8_t {
			WAITING = 0,
			WORKING,

			TOTAL
		};


		clock_t state_started_at;
		inline void ReStart() {
			for (auto& time: timeSpent)
				time = 0;
			fullTimeSpent = clock();

			active = true;
			status = StatusFlag::WAITING;
			state_started_at = clock();
		}

		inline void Set(StatusFlag flag) {
			if (status != flag) {
				// Update
				clock_t now = clock();
				timeSpent[status] += now - state_started_at;
				state_started_at = now;

				status = flag;
			}
		}
	
		inline void Stop() {
			if (active) {
				clock_t now = clock();
				timeSpent[status] += now - state_started_at;
				fullTimeSpent = now - fullTimeSpent;
				state_started_at = 0;
				active = false;
			}
		}

		std::string ToString() {
			constexpr clock_t ClocksPerMS = CLOCKS_PER_SEC/1000;
			return fmt::format ("TotalRuntime: {}ms [Working: {}ms {:.2f}%; Waiting: {}ms {:.2f}%]"
				, fullTimeSpent/ClocksPerMS
				, timeSpent[StatusFlag::WORKING]/ClocksPerMS, (double(timeSpent[StatusFlag::WORKING])/double(fullTimeSpent))*100.0
				, timeSpent[StatusFlag::WAITING]/ClocksPerMS, (double(timeSpent[StatusFlag::WAITING])/double(fullTimeSpent))*100.0
			);
		}

		double Get_TotalTimeInSeconds() { // for last run
			constexpr double ClocksPerSec = CLOCKS_PER_SEC;
			if (!active)
				return double (fullTimeSpent) / ClocksPerSec;
			return 0.0;
		}
		double Get_WorkingTimeInSeconds() {
			constexpr double ClocksPerSec = CLOCKS_PER_SEC;
			return double (timeSpent[StatusFlag::WORKING]) / ClocksPerSec;
		}
		double Get_WaitingTimeInSeconds() {
			constexpr double ClocksPerSec = CLOCKS_PER_SEC;
			return double (timeSpent[StatusFlag::WAITING]) / ClocksPerSec;
		}

	private:
		clock_t timeSpent[StatusFlag::TOTAL];
		clock_t fullTimeSpent;

		StatusFlag status;
		bool active = false;
	};
};

struct ADummyOperatableBlock {

	ADummyOperatableBlock(void* data_block = nullptr)
		:user_data(data_block)
	{
		stack.reserve (18);
	};

	ADummyOperatableBlock(const ADummyOperatableBlock& in) = delete;

	ADummyOperatableBlock(ADummyOperatableBlock&& in)
		: m_SpawnedAtTime (in.m_SpawnedAtTime), user_data (in.user_data), stack(std::move (in.stack))
	{
		assert(in.m_SpawnedAtTime != 0);
		in.m_SpawnedAtTime = 0;
		in.user_data = nullptr;
	}

	void operator = (ADummyOperatableBlock&& in) {
		m_SpawnedAtTime = in.m_SpawnedAtTime, user_data = in.user_data, stack = std::move (in.stack);

		// assert(in.m_SpawnedAtTime != 0);

		in.m_SpawnedAtTime = 0, in.user_data = nullptr;
	}


	const void* Data() const {
		return user_data;
	}

	void SetData (void* in) {
		user_data = in;
	}

	void AddToStack (std::string_view func_name) const {
		if (stack.capacity () == stack.size ())
			stack.reserve (stack.capacity () + 24);
		stack.push_back (func_name);
	}

	std::vector<std::string_view>& GetStack_raw () const {
		return stack;
	}

	clock_t TimeSinceSpawn() const {
		if (m_SpawnedAtTime != 0) {
			constexpr auto clock_per_ms = CLOCKS_PER_SEC/1000;
			return (clock() - m_SpawnedAtTime)/clock_per_ms;
		}
		return 0;
	}

	~ADummyOperatableBlock() {
		stack.clear ();
		user_data = nullptr;
		m_SpawnedAtTime = 0;
	}
private:
	mutable std::vector <std::string_view> stack;
	clock_t m_SpawnedAtTime = clock ();
	void* user_data;
};

void ADummyOperation(ADummyOperatableBlock& block) {
	 std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

//###################################################
//# CHANNELS(Queues with different producer consumer)
template <typename Typ>
class Channel {
public:
	Channel (void)
		: q (), m (), c () {}

	~Channel (void) {}

	// Move an element to the queue.
	FORCEINLINE void move_in (Typ t) {
		std::lock_guard <std::mutex> lock (m);
		q.push (std::move (t));
		c.notify_one ();
	}

	// Extract the "front"-element.
	// If the queue is empty, wait till a element is avaiable.
	FORCEINLINE Typ move_out (void) {
		std::unique_lock <std::mutex> lock (m);
		while (q.empty ()) {
			// release lock as long as the wait and reaquire it afterwards.
			c.wait (lock);
		}
		Typ val = std::move (q.front ());
		q.pop ();
		return std::move(val);
	}

	// Try-extract the "front"-element.
	// If the queue is empty, return false.
	FORCEINLINE bool try_move_out (Typ&& out) {
		std::unique_lock <std::mutex> lock (m);
		if (!q.empty ()) {
			out = std::move (q.front ());
			q.pop ();
			return true;
		}

		return false;
	}

	FORCEINLINE const Typ* peek () {
		std::unique_lock <std::mutex> lock (m);
		if (!q.empty ()) {
			return &q.front ();
		}
		return nullptr;
	}

private:
	std::queue <Typ>        q;
	mutable std::mutex      m;
	std::condition_variable c;
};