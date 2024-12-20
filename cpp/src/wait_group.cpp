#include "stdafx.hpp"
#include <assert.h>
#include "../../c/src/common/atomic.h"
#include "fiber/fiber_tbox.hpp"
#include "fiber/wait_group.hpp"

namespace acl {

wait_group::wait_group()
{
	box_   = new acl::fiber_tbox<unsigned long>;
	state_ = (void*) atomic_new();
	n_     = 0;

	atomic_set((ATOMIC*) state_, &n_);
	atomic_int64_set((ATOMIC*) state_, n_);
}

wait_group::~wait_group()
{
	delete box_;
	atomic_free((ATOMIC*) state_);
}

void wait_group::add(int n)
{
	long long state = atomic_int64_add_fetch((ATOMIC*) state_,
			(long long) n << 32);

	//��32λΪ��������
	int c = (int)(state >> 32);

	//��32λΪ�ȴ�������
	unsigned w =  (unsigned) state;

	//count����С��0
	if (c < 0){
		msg_fatal("Negative wait_group counter");
	}

	if (w != 0 && n > 0 && c == n){
		msg_fatal("Add called concurrently with wait");
	}

	if (c > 0 || w == 0) {
		return;
	}

	//���state�Ƿ��޸�
	if (atomic_int64_fetch_add((ATOMIC*) state_, 0) != state) {
		msg_fatal("Add called concurrently with wait");
	}

	//����countΪ0�ˣ����state���������еȴ���
	atomic_int64_set((ATOMIC*) state_, 0);

	for (size_t i = 0; i < w; i++) {
#ifdef	_DEBUG
		unsigned long* tid = new unsigned long;
		*tid = acl::thread::self();
		box_->push(tid);
#else
		box_->push(NULL);
#endif
	}
}

void wait_group::done()
{
	add(-1);
}

void wait_group::wait()
{
	for(;;) {
		long long state = atomic_int64_fetch_add((ATOMIC*) state_, 0);
		int c = (int) (state >> 32);

		//û������ֱ�ӷ���
		if (c == 0) {
			return;
		}

		//�ȴ���������һ��ʧ�ܵĻ����»�ȡstate
		if (atomic_int64_cas((ATOMIC*) state_, state, state + 1) == state) {
			bool found;
#ifdef	_DEBUG
			unsigned long* tid = box_->pop(-1, &found);
			assert(found);
			delete tid;
#else
			(void) box_->pop(-1, &found);
			assert(found);
#endif
			if(atomic_int64_fetch_add((ATOMIC*) state_, 0) == 0) {
				return;
			}

			msg_fatal("Reused before previous wait has returned");
		}
	}
}

} // namespace acl
