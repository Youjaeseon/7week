/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void do_donate(void);
static void remove_donated(struct lock *lock);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level; // 함수 진입 전에 인터럽트가 어떤 상태였는지 기억하기 위한 변수.

	ASSERT (sema != NULL);
	ASSERT (!intr_context ()); // 현재 실행 위치가 인터럽트 핸들러 내부인지 확인.

	old_level = intr_disable (); // 인터럽트 끄기. 인터럽트로 인해 실행이 중단되고 다른 스레드가 끼어들 수 있다.(원자성 보장.)
	while (sema->value == 0) {  // Semaphore 0인 동안 - if문이 아닌 이유는? sema_up을 했다고 해도 semaphore의 허가를 얻은것이 아님.
		//list_push_back (&sema->waiters, &thread_current ()->elem); // Sema->waitres에 현재 스레드 대기상태로 추가. 같은 elem을 동시에 여러 리스트에 넣으면 안된다.
		
		list_insert_ordered(&sema->waiters,&thread_current()->elem,compare_priority,NULL); // 리스트에 원소 넣을 때 정렬 기준에 맞는 위치를 찾아 삽입하는 함수

		thread_block (); // A가 BLOCKED 되고 다른 스레드 실행. 이후 A가 CPU를 얻으면 thread_block 재개
		//thread_block의 역할.
		//현재 스레드를 BLOCKED 상태로 만든다.
		//스케줄러를 호출한다.
		//다른 실행 가능한 스레드를 선택한다.
		//문맥 교환을 수행한다.
	}
	sema->value--;  //아닐시 value -1해서 허가. 
	intr_set_level (old_level); // 저장해 두었던 인터럽트 상태 복원.
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {  // 허가가 없을 때 기다리지 않음.
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();

//	if (!list_empty (&sema->waiters))
//		thread_unblock (list_entry (list_pop_front (&sema->waiters),  //리스트 맨 앞 요소 제거 후 포인터 반환. list_entry로 thread 구조체 주소 구하기.
//					struct thread, elem));                            // 즉, 빈스트가 아니면  semaphore 대기 리스트의 맨 앞 스레드를 READY상태로 만들기.(우선순위 검사가 없네?? 이거 문제있다. 현재 FIFO로 꺼내올 듯.)
	sema->value++;
	if (!list_empty (&sema->waiters)) {
		struct thread *t = thread_current();
		struct thread *waiter_front = list_entry(list_front(&sema->waiters), struct thread, elem);
		
		list_sort(&sema->waiters, compare_priority, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
		struct thread, elem));
		if (t->priority < waiter_front->priority) {
			if(intr_context())
				intr_yield_on_return();
			else
				thread_yield();
		}
	}
	intr_set_level (old_level);
	thread_check_preemption();
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void  // 두 스레드의 실행 순서를 맞추는 신호로 활용.
sema_self_test (void) {
	struct semaphore sema[2]; // 메인 스레드가 보조 스레드에게, 보조 스레드가 메인 스레드에게 보내는 신호라서 2개가 필요함. 초기값 0
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void // lock은 특정 공유 자원에 한 번에 하나의 스레드만 접근하도록 보호하는 도구. 임계구역 필요(acquire와 release 사이).
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL; //lock을 소유한 스레드가 없음.
	sema_init (&lock->semaphore, 1); //lock 내부 semaphore의 허가 개수 1.
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	struct thread *t = thread_current();
	if(lock->holder != NULL && (lock->holder->priority < t->priority)) { // 도네이션 받는 조건
		t->wait_on_lock = lock; // 내가 기다리고 있는 락 저장.
		list_insert_ordered(&(lock->holder->donors),&t->elem_donor, compare_donate_priority, NULL); // 나한테 도네 준 스레드 저장
		// lock->holder->priority = t->priority; // 도네 받기
		do_donate();
	}
	sema_down (&lock->semaphore);
	t->wait_on_lock = NULL;
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool  // 왜 필요한 함수인건지 확인하기.
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock)); //현재 쓰레드가 소유자가 맞는지.

	remove_donated(lock); // 도네 받은거 반환.
	refresh_priority(); // holder 우선순위 재계산.

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}


/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool  //lock 소유 여부 확인.
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}  //Donation을 구현하려면 Lock을 기다리는 스레드, 소유자, 기부받은 우선순위 등을 별도로 관리하는 로직이 필요.

/* One semaphore in a list. */
// struct semaphore_elem {
// 	struct list_elem elem;              /* List element. */
// 	struct semaphore semaphore;         /* This semaphore. */
// };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
} 

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter; //stack에 생성되는 지역변수. BLOCKED 되더라도 스택이 사라지지 않음!!

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock); // 대기중일때 lock을 계속 소유중이면 다른 쓰레드들도 대기해야 함.
	sema_down (&waiter.semaphore);  // 쓰레드가 BLOCKED 되면 함수는 여기서 멈춤. 추후 다시 CPU를 받고 아래 함수 실행.
	lock_acquire (lock);	
}

static bool //struct semaphore_elem 안에 들어 있는 세마포어의 대기 스레드 우선순위를 비교
cond_priority_less(const struct list_elem *a,
                   const struct list_elem *b,
                   void *aux UNUSED)
{
    struct semaphore_elem *sa =
        list_entry(a, struct semaphore_elem, elem);

    struct semaphore_elem *sb =
        list_entry(b, struct semaphore_elem, elem);

    struct thread *ta =
        list_entry(list_front(&sa->semaphore.waiters),
                   struct thread, elem);

    struct thread *tb =
        list_entry(list_front(&sb->semaphore.waiters),
                   struct thread, elem);

    return ta->priority > tb->priority;
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	// if (!list_empty (&cond->waiters)) //대기자가 있는지 확인.
	// 	sema_up (&list_entry (list_pop_front (&cond->waiters),
	// 				struct semaphore_elem, elem)->semaphore);

	if (!list_empty(&cond->waiters)) {
    list_sort(&cond->waiters,
              cond_priority_less,
              NULL);

    struct semaphore_elem *waiter =
        list_entry(list_pop_front(&cond->waiters),
                   struct semaphore_elem, elem);

    sema_up(&waiter->semaphore);
   }
}
/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void  // 모든 쓰레드에게 신호.
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	// list_sort(&(cond->waiters), compare_cond_priority, NULL);
	list_sort(&cond->waiters,
              cond_priority_less,
              NULL);
	while (!list_empty (&cond->waiters)){
		struct semaphore_elem *waiter =
		list_entry(list_pop_front(&cond->waiters),
				struct semaphore_elem, elem);

   	sema_up(&waiter->semaphore);
	}
}


static void
remove_donated(struct lock *lock) {
	struct thread *holder = lock->holder;
	if (list_empty(&holder->donors))
		return ; 

	// else {
	// 	holder->priority = list_entry(list_pop_front(&holder->donors), struct thread, elem_donor)->priority;
	// }
	struct list_elem *elem_dono = list_begin(&holder->donors); // 홀더가 받은 도네 리스트
	struct thread *donor ;
	while (elem_dono != list_end(&holder->donors)) {
		struct list_elem *next = list_next(elem_dono); // 다음 원소로 미리 저장. 꺼내고 이동하는 게 문제가 될 수 있음.
		donor = list_entry(elem_dono, struct thread, elem_donor); // 순회를 위한 꺼내기.

		if (donor->wait_on_lock == lock) { // 현재 락을 기다리던 스레드면
			list_remove(elem_dono); // 도네이터 목록에서 빼고.
		}
		elem_dono = next; // 다음 원소로 이동.

	}
}

static void
do_donate(void) {
	struct thread *t = thread_current();
	struct thread *holder;
	/* 
	 * 스레드 t의 wait_on_lock의 holder는 t가 도네이션 한 스레드임.
	 * 그 holder의 wait_on_lock 의 holder는 holder가 도네이션 한 스레드임.
	 * 반복문으로 중첩 도네이션 계산하는 원리.
	 */
	while(t->wait_on_lock != NULL) { // 내가 기다리고 있는 락의 소유자에게 도네
		holder = t->wait_on_lock->holder;
		holder->priority = t->priority;
		t = holder;
	}
}

void
refresh_priority(void) {
	struct thread *curr = thread_current();
	// 나에게 도네이션 한 스레드가 없으면 원래 우선순위로 돌아가기.
	if (list_empty(&curr->donors))
		curr->priority = curr->old_priority;
	// 나에게 도네이션 한 스레드가 있으면, 그 리스트의 가장 앞 원소의 우선순위 따르기 (정렬되어 있기 때문에)
	else {
		curr->priority = list_entry(list_front(&curr->donors), struct thread, elem_donor)->priority;
	}
}