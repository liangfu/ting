/* The MIT License:

Copyright (c) 2011-2012 Ivan Gagis <igagis@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE. */

// Home page: http://ting.googlecode.com



/**
 * @author Ivan Gagis <igagis@gmail.com>
 * @brief Atomic operations.
 */

#pragma once

#include "config.hpp"
#include "debug.hpp"
#include "types.hpp"
#include "util.hpp"
#include "mt/Thread.hpp"



#if M_CPU == M_CPU_X86 || \
		M_CPU == M_CPU_X86_64 || \
		(M_CPU == M_CPU_ARM && M_CPU_ARM_THUMB != 1)

#else
#	define M_ATOMIC_USE_MUTEX_FALLBACK
#	include "mt/Mutex.hpp"
#endif



namespace ting{
namespace atomic{

//forward declarations
class SpinLock;
class S32;



/**
 * @brief Atomic flag.
 * Atomic flag is a bool-like value whose set and clear operations are atomic.
 */
//On most architectures, atomic operations require that the value to be naturally aligned (4 bytes = sizeof(int)).
#ifndef M_DOXYGEN_DONT_EXTRACT //for doxygen

//make sure theat we align by int size when using MSVC compiler.
STATIC_ASSERT(sizeof(int) == 4)

M_DECLARE_ALIGNED_MSVC(4)
#endif
class Flag{
	//declare these classes as friends so they will be able to access the memory barrier function
	friend class atomic::SpinLock;
	friend class atomic::S32;

#if M_OS == M_OS_WINDOWS
#	if defined(MemoryBarrier)
#		undef MemoryBarrier
#	endif
#endif

	inline static void MemoryBarrier()throw(){
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
		//do nothing

#elif M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64
		//do nothing, because locked operations on x86 make memory barrier

#elif M_CPU == M_CPU_ARM && M_CPU_VERSION >= 7 && M_CPU_ARM_THUMB != 1 //DMB instruction is available only on ARMv7
		__asm__ __volatile__(
				"dmb" : : :"memory" //modifies "memory" is for compiler barrier to avoid instruction reordering by compiler
			);

#elif M_CPU == M_CPU_ARM && M_CPU_ARM_THUMB != 1 //for older ARMs use CP15 data memory barrier operation
		__asm__ __volatile__(
				"mcr p15, 0, %0, c7, c10, 5" : :"r"(1) :"memory" //modifies "memory" is for compiler barrier to avoid instruction reordering by compiler
			);

#elif M_CPU == M_CPU_ARM && M_CPU_ARM_THUMB == 1
#	error "ARM Thumb-1 mode does not support atomic operations"

#else
#	error "ASSERT(false)"
#endif
	}



#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
	ting::mt::Mutex mutex;
	volatile bool flag;
#elif M_CPU == M_CPU_X86 || \
		M_CPU == M_CPU_X86_64 || \
		M_CPU == M_CPU_ARM

	volatile int flag;

#else
#	error "ASSERT(false)"
#endif

public:
	/**
	 * @brief Constructor.
	 * @param initialValue - initial value of the flag.
	 */
	inline Flag(bool initialValue = false){
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK) || \
		M_CPU == M_CPU_X86 || \
		M_CPU == M_CPU_X86_64 || \
		M_CPU == M_CPU_ARM

		this->Set(initialValue);

#else
#	error "ASSERT(false)"
#endif
	}



	/**
	 * @brief Gets the current flag value.
	 * Note, that the returned value may be not actual, since the flag value can
	 * be changed in parallel. It does not set any memory barrier.
	 * @return current flag value.
	 */
	inline bool Get()const throw(){
#if M_COMPILER == M_COMPILER_MSVC
		return this->flag == 0 ? false : true; //this is to avoid compiler warning
#else
		return this->flag;
#endif
	}



	/**
	 * @brief Set the flag value.
	 * Sets the flag to the new value and returns its previous value as atomic operation.
	 * It does not set any memory barrier.
	 * @param value - the flag value to set.
	 * @return old flag value.
	 */
	inline bool Set(bool value = true)throw(){
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
		{
			ting::mt::Mutex::Guard mutexGuard(this->mutex);
			bool old = this->flag;
			this->flag = value;
			return old;
		}
#elif M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64
		int old;
#	if M_COMPILER == M_COMPILER_MSVC
		__asm{
			mov ebx, this
			xor eax, eax
			mov al, value
			lock xchg eax, [ebx].flag
			mov [old], eax
		}
		return old == 0 ? false : true; // this ternary ? : stuff is to avoid compiler warning
#	else
		__asm__ __volatile__(
				"lock; xchgl %0, %1"
						: "=r"(old), "=m"(this->flag)
						: "0"(int(value)), "m"(this->flag)
						: "memory"
			);
		return old;
#	endif

#elif M_CPU == M_CPU_ARM
		int old;
#	if M_CPU_VERSION >= 6 //should support ldrex/strex instructions unless Thumb-1 mode is used
#		if M_CPU_ARM_THUMB == 1 //Thumb-1 mode does not support ldrex/strex instructions, use interrupts disabling
#			error "Not implemented"
		ting::u32 tmp;
		__asm__ __volatile__(
				"mrs    %0, PRIMASK" "\n" //save interrupts mask
				"cpsid  i"           "\n" //disable pioratizable interrupts
				"mov    %3, %1"      "\n"
				"mov    %2, %3"      "\n"
				"msr    PRIMASK, %0" "\n" //restore interrupts mask
						: "=r"(tmp), "=r"(old)
						: "r"(value), "r"(&this->flag)
						: "memory"
			);
#		else //Thumb2 or not thumb mode at all
		int res;
		__asm__ __volatile__(
				"1:"                       "\n"
				"	ldrex    %0, [%3]"     "\n"
				"	strex    %1, %2, [%3]" "\n"
				"	teq      %1, #0"       "\n"
				"	bne      1b"           "\n"
						: "=&r"(old), "=&r"(res) //res is not used and because of that GCC tends to assign the same register to it as for the the next argument, adding this & early clobber prevents this.
						: "r"(value), "r"(&this->flag)
						: "cc", "memory" // "cc" stands for "condition codes"
			);

#		endif
#	else // ARM older than v6
		__asm__ __volatile__(
				"swp %0, %1, [%2]"
						: "=&r"(old)
						: "r"(value), "r"(&this->flag)
						: "memory"
			);
#	endif
		return old;

#else //unknown cpu architecture, will be using plain mutex
#	error "ASSERT(false)"
#endif
	}


	/**
	 * @brief Set the flag value with acquire memory semantics.
	 * Sets the flag to the new value and returns its previous value as atomic operation.
	 * It sets acquire memory semantics barrier. It means that on weakly ordered architectures
	 * memory access operations which go after the SetAcquire() will not be executed before it.
	 * @param value - the flag value to set.
	 * @return old flag value.
	 */
	inline bool SetAcquire(bool value = true)throw(){
		bool ret = this->Set(value);
		atomic::Flag::MemoryBarrier();
		return ret;
	}



	/**
	 * @brief Set the flag value with release memory semantics.
	 * Sets the flag to the new value and returns its previous value as atomic operation.
	 * It sets release memory semantics barrier. It means that on weakly ordered architectures
	 * memory access operations which go before the SetRelease() will not be executed after it.
	 * @param value - the flag value to set.
	 * @return old flag value.
	 */
	inline bool SetRelease(bool value = true)throw(){
		atomic::Flag::MemoryBarrier();
		return this->Set(value);
	}
	
	
	
	/**
	 * @brief Clear flag.
	 * Basically, it is equivalent to Flag::Set(false), but on some architectures
	 * its implementation can be faster.
	 * It does not set any memory barrier.
	 */
	inline void Clear()throw(){
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
		{
			//still need to lock the mutex to generate the memory barrier.
			ting::mt::Mutex::Guard mutexGuard(this->mutex);
			this->flag = false;
		}
#elif M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64 || M_CPU == M_CPU_ARM
		this->Set(false);

#else //unknown cpu architecture
#	error "ASSERT(false)"
#endif
	}
	
	
	
	/**
	 * @brief Clear flag with acquire memory semantics.
	 * Basically, it is equivalent to Flag::Set(false), but on some architectures
	 * its implementation can be faster.
	 * It sets acquire memory semantics barrier. It means that on weakly ordered architectures
	 * memory access operations which go after the ClearAcquire() will be executed exactly after it.
	 */
	inline void ClearAcquire()throw(){
		this->Clear();
		Flag::MemoryBarrier();
	}
	
	
	
	/**
	 * @brief Clear flag with release memory semantics.
	 * Basically, it is equivalent to Flag::Set(false), but on some architectures
	 * its implementation can be faster.
	 * It sets release memory semantics barrier. It means that on weakly ordered architectures
	 * memory access operations which go before the ClearRelease() will be executed exactly before it.
	 */
	inline void ClearRelease()throw(){
		Flag::MemoryBarrier();
		this->Clear();
	}
} M_DECLARE_ALIGNED(sizeof(int)); //On most architectures, atomic operations require that the value to be naturally aligned.



/**
 * @brief Spinlock class.
 * Spinlock is the same thing as mutex, except that when trying to lock a locked
 * spinlock it enters a busy-loop until the spinlock is unlocked by previous locker so
 * it can be locked. But, in return, spinlock is much more lightweight than mutex,
 * because it does not do any syscalls.
 * Use spinlock to synchronize only very short and rare operations. In other cases, use mutex.
 */
//On most architectures, atomic operations require that the value to be naturally aligned (4 bytes = sizeof(int)).
#ifndef M_DOXYGEN_DONT_EXTRACT //for doxygen

//make sure theat we align by int size when using MSVC compiler.
STATIC_ASSERT(sizeof(int) == 4)

M_DECLARE_ALIGNED_MSVC(4)
#endif
class SpinLock{
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
	ting::mt::Mutex mutex;
#elif M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64 || M_CPU == M_CPU_ARM

	atomic::Flag flag;

#else //unknown cpu architecture, will be using plain mutex
#	error "ASSERT(false)"
#endif


public:
	/**
	 * @brief Constructor.
	 * Creates an initially unlocked spinlock.
	 */
	inline SpinLock(){
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
		//no need to initialize mutex, it is unlocked initially itself.

#elif M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64 || M_CPU == M_CPU_ARM

		//initially unlocked.

#else //unknown cpu architecture, will be using plain mutex
#	error "ASSERT(false)"
#endif
	}



	/**
	 * @brief Lock the spinlock.
	 * If the lock cannot be acquired immediately it will enter a busy loop
	 * reading the lock value until it becomes 'unlocked' and then try to lock again.
	 * Right after acquiring the lock the memory barrier is set.
	 */
	inline void Lock()throw(){
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
		this->mutex.Lock();
#elif M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64 || M_CPU == M_CPU_ARM

		while(this->flag.Set(true)){
			while(this->flag.Get()){}
		}
		atomic::Flag::MemoryBarrier();

#else
#	error "ASSERT(false)"
#endif
	}

	
	
	/**
	 * @brief Lock the spinlock.
	 * This will do Thread::Sleep(0) in the busy loop if the lock cannot be acquired immediately.
	 * Right after acquiring the lock the memory barrier is set.
	 */
	inline void LockYield()throw(){
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
		this->mutex.Lock();
#elif M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64 || M_CPU == M_CPU_ARM

		while(this->flag.Set(true)){
			ting::mt::Thread::Sleep(0);
		}
		atomic::Flag::MemoryBarrier();

#else
#	error "ASSERT(false)"
#endif
	}
	


	/**
	 * @brief Unlock the spinlock.
	 * Right before releasing the lock the memory barrier is set.
	 */
	inline void Unlock()throw(){
#if defined(M_ATOMIC_USE_MUTEX_FALLBACK)
		this->mutex.Unlock();
#elif M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64 || M_CPU == M_CPU_ARM

		atomic::Flag::MemoryBarrier();
		this->flag.Clear();

#else
#	error "ASSERT(false)"
#endif
	}
	
	
	
	/**
	 * @brief Helper class which automatically Locks the given spinlock.
	 * This helper class automatically locks the given spinlock in the constructor and
	 * unlocks the spinlock in destructor. This class is useful if the code between
	 * spinlock lock/unlock may return or throw an exception,
	 * then the spinlock will be automatically unlocked in such case.
	 */
	class Guard{
		SpinLock& sl;
	public:
		Guard(SpinLock& sl)throw() :
				sl(sl)
		{
			this->sl.Lock();
		}
		
		~Guard()throw(){
			this->sl.Unlock();
		}
	};
	
	
	
	/**
	 * @brief Helper class which automatically Locks the given spinlock.
	 * This is the same as SpinLock::Guard except it locks the spinlock with
	 * SpinLock::LockYield() method.
	 */
	class GuardYield{
		SpinLock& sl;
	public:
		GuardYield(SpinLock& sl)throw() :
				sl(sl)
		{
			this->sl.LockYield();
		}
		
		~GuardYield()throw(){
			this->sl.Unlock();
		}
	};
} M_DECLARE_ALIGNED(sizeof(int)); //On most architectures, atomic operations require that the value to be naturally aligned.



/**
 * @brief Atomic signed 32bit integer.
 */
//On most architectures, atomic operations require that the value to be naturally aligned (4 bytes = sizeof(int)).
#ifndef M_DOXYGEN_DONT_EXTRACT //for doxygen

//make sure theat we align by int size when using MSVC compiler.
STATIC_ASSERT(sizeof(int) == 4)

M_DECLARE_ALIGNED_MSVC(4)
#endif
class S32{

#if M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64 || \
	(M_CPU == M_CPU_ARM  && M_CPU_VERSION >= 6 && M_CPU_ARM_THUMB != 1)

	//no additional variables required
#else
	atomic::SpinLock spinLock;
#endif

	volatile ting::s32 v;

public:
	/**
	 * @brief Constructor.
	 * @param initialValue - initial value to assign to this atomic variable.
	 */
	inline S32(ting::s32 initialValue = 0) :
			v(initialValue)
	{}



	inline ~S32()throw(){}



	/**
	 * @brief Adds the value to this atomic variable and returns its initial value.
	 * It does not set any memory barrier.
	 * @param value - the value to add to this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::s32 FetchAndAdd(ting::s32 value)throw(){
#if M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64

		{
			ting::s32 old;
#	if M_COMPILER == M_COMPILER_MSVC
			__asm{
				mov ebx, this
				mov eax, [value]
				lock xadd [ebx].v, eax
				mov [old], eax
			}
#	else
			__asm__ __volatile__ (
					"lock; xaddl %0, %1"
							: "=r"(old), "=m"(this->v)
							: "0"(value), "m"(this->v)
							: "memory"
				);
#	endif
			return old;
		}

#elif M_CPU == M_CPU_ARM && M_CPU_VERSION >= 6 && M_CPU_ARM_THUMB != 1
		ting::s32 old, sum;
		int res;
		__asm__ __volatile__(
				"1:"                         "\n"
				"	ldrex   %0, [%4]"        "\n" // load old value
				"	add     %2, %0, %3"      "\n" // %3 = %0 + %2 NOTE: in case of storing failure need to do the addition again, since the old value has probably changed
				"	strex   %1, %2, [%4]"    "\n" // store new value
				"	teq     %1, #0"          "\n" // check if storing the value has succeeded (compare %1 with 0)
				"	bne     1b"              "\n" // jump to label 1 backwards (to the beginning) to try again if %1 is not 0, i.e. storing has failed
						: "=&r"(old), "=&r"(res), "=&r"(sum)  //res is not used, thus we need this & early-clobber to avoid gcc assign the same register to it as to something else.
						: "r"(value), "r"(&this->v)
						: "cc", "memory" //"cc" = "condition codes"
			);
		return old;

#else
		this->spinLock.Lock();
		ting::s32 old = this->v;
		this->v += value;
		this->spinLock.Unlock();
		return old;
#endif
	}

	
	
	/**
	 * @brief Adds the value to this atomic variable and returns its initial value.
	 * It sets acquire memory semantics barrier.
	 * @param value - the value to add to this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::s32 FetchAndAddAcquire(ting::s32 value)throw(){
		ting::s32 ret = this->FetchAndAdd(value);
		atomic::Flag::MemoryBarrier();
		return ret;
	}
	
	
	
	/**
	 * @brief Adds the value to this atomic variable and returns its initial value.
	 * It sets release memory semantics barrier.
	 * @param value - the value to add to this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::s32 FetchAndAddRelease(ting::s32 value)throw(){
		atomic::Flag::MemoryBarrier();
		return this->FetchAndAdd(value);
	}
	


	/**
	 * @brief Atomic compare and exchange operation.
	 * Compares the current value to the 'compareTo' value and if they are equal
	 * it will store the 'exchangeBy' value to the current value.
	 * It does not set any memory barrier.
	 * @param compareTo - the value to compare the current value to.
	 * @param exchangeBy - the value to store as the the current value in case the comparison will result in equals.
	 *                     Otherwise, the current value will remain untouched.
	 * @return Previous value.
	 */
	inline ting::s32 CompareAndExchange(ting::s32 compareTo, ting::s32 exchangeBy)throw(){
#if M_CPU == M_CPU_X86 || M_CPU == M_CPU_X86_64

		ting::s32 old;
#	if M_COMPILER == M_COMPILER_MSVC
		__asm{
			mov ebx, this
			mov eax, [compareTo]
			mov edx, [exchangeBy]
			lock cmpxchg [ebx].v, edx
			mov [old], eax
		}
#	else
		__asm__ __volatile__(
				"lock; cmpxchgl %3, %2"
						: "=m"(this->v), "=a"(old) // 'a' is 'eax' or 'ax' or 'al' or 'ah'
						: "m"(this->v), "r"(exchangeBy), "a"(compareTo)
						: "memory"
			);
#	endif
		return old;

#elif M_CPU == M_CPU_ARM && M_CPU_VERSION >= 6 && M_CPU_ARM_THUMB != 1
		ting::s32 old;
		int res;
#	if M_CPU_ARM_THUMB == 2
		__asm__ __volatile__(
				"1:"                         "\n"
				"	ldrex   %0, [%4]"        "\n" //load old value
				"	teq     %0, %2"          "\n" //test for equality (xor operation)
				"	ite     eq"              "\n" //if equal
				"	strexeq %1, %3, [%4]"    "\n" //if equal, then store exchangeBy value
				"	bne     2f"              "\n" //otherwise, jump to exit with clearing exclusive access, NOTE: Branching must be last instruction in IT block!
				"	teq     %1, #0"          "\n" //check if storing the value has succeeded (compare %1 with 0)
				"	bne     1b"              "\n" //jump to label 1 backwards (to the beginning) to try again if %1 is not 0, i.e. storing has failed
				"	b       3f"              "\n" //jump to label 3 forward (exit) if succeeded
				"2:"                         "\n"
#		if M_CPU_VERSION >= 7                          // CLREX instruction for Thumb-2 is only supported in ARMv7
				"	clrex"                   "\n" //was not equal, clear exclusive access
#		else
				"	strex   %1, %0, [%4]"    "\n" //store previous value, we don't care if it fails, since we just need to clear exclusive access
#		endif
				"3:"                         "\n"
						: "=&r"(old), "=&r"(res)  //res is not used, thus we need this & early-clobber to avoid gcc assign the same register to it as to something else.
						: "r"(compareTo), "r"(exchangeBy), "r"(&this->v)
						: "cc", "memory" // "cc" = "condition codes"
			);
#	else //non-Thumb 2 mode
#		if M_CPU_ARM_THUMB != 0
			#error "ASSERT(false)"
#		endif
		__asm__ __volatile__(
				"1:"                         "\n"
				"	ldrex   %0, [%4]"        "\n" //load old value
				"	teq     %0, %2"          "\n" //test for equality (xor operation)
				"   bne     2f"              "\n" //jump to exit with clearing exclusive access
				"	strexeq %1, %3, [%4]"    "\n" //if equal, then store exchangeBy value
				"	teq     %1, #0"          "\n" //check if storing the value has succeeded (compare %1 with 0)
				"	bne     1b"              "\n" //jump to label 1 backwards (to the beginning) to try again if %1 is not 0, i.e. storing has failed
				"	b       3f"              "\n" //jump to exit if succeeded
				"2:"                         "\n"
#		if M_CPU_VERSION >= 7                          // CLREX instruction for ARM is supported in ARMv6K and higher, we don't detect this K and treat it as it is available from ARMv7
				"	clrex"                   "\n" //was not equal, clear exclusive access
#		else
				"	strex   %1, %0, [%4]"    "\n" //store previous value, we don't care if it fails, since we just need to clear exclusive access
#		endif
				"3:"
						: "=&r"(old), "=&r"(res)  //res is not used, thus we need this & early-clobber to avoid gcc assign the same register to it as to something else.
						: "r"(compareTo), "r"(exchangeBy), "r"(&this->v)
						: "cc", "memory" // "cc" = "condition codes"
			);
#	endif
		return old;

#else
		this->spinLock.Lock();
		ting::s32 old = this->v;
		if(old == compareTo){
			this->v = exchangeBy;
		}
		this->spinLock.Unlock();
		return old;
#endif
	}
	
	
	
	/**
	 * @brief Atomic compare and exchange operation.
	 * Compares the current value to the 'compareTo' value and if they are equal
	 * it will store the 'exchangeBy' value to the current value.
	 * It sets acquire memory semantics barrier.
	 * @param compareTo - the value to compare the current value to.
	 * @param exchangeBy - the value to store as the the current value in case the comparison will result in equals.
	 *                     Otherwise, the current value will remain untouched.
	 * @return Previous value.
	 */
	inline ting::s32 CompareAndExchangeAcquire(ting::s32 compareTo, ting::s32 exchangeBy)throw(){
		ting::s32 ret = CompareAndExchange(compareTo, exchangeBy);
		atomic::Flag::MemoryBarrier();
		return ret;
	}
	
	
	
	/**
	 * @brief Atomic compare and exchange operation.
	 * Compares the current value to the 'compareTo' value and if they are equal
	 * it will store the 'exchangeBy' value to the current value.
	 * It sets release memory semantics barrier.
	 * @param compareTo - the value to compare the current value to.
	 * @param exchangeBy - the value to store as the the current value in case the comparison will result in equals.
	 *                     Otherwise, the current value will remain untouched.
	 * @return Previous value.
	 */
	inline ting::s32 CompareAndExchangeRelease(ting::s32 compareTo, ting::s32 exchangeBy)throw(){
		atomic::Flag::MemoryBarrier();
		return CompareAndExchange(compareTo, exchangeBy);
	}
	
} M_DECLARE_ALIGNED(sizeof(int)); //On most architectures, atomic operations require that the value to be naturally aligned.



/**
 * @brief Atomic unsigned 32bit integer.
 */
class U32{
	atomic::S32 v;
public:
	/**
	 * @brief Constructor.
	 * @param initialValue - initial value to assign to this atomic variable.
	 */
	inline U32(ting::u32 initialValue = 0) :
			v(ting::s32(initialValue))
	{}
			
	inline ~U32()throw(){}
	
	/**
	 * @brief Adds the value to this atomic variable and returns its initial value.
	 * It does not set any memory barrier.
	 * @param value - the value to add to this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::u32 FetchAndAdd(ting::u32 value)throw(){
		return ting::u32(this->v.FetchAndAdd(ting::s32(value)));
	}
	
	/**
	 * @brief Adds the value to this atomic variable and returns its initial value.
	 * It sets acquire memory semantics barrier.
	 * @param value - the value to add to this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::u32 FetchAndAddAcquire(ting::u32 value)throw(){
		return ting::u32(this->v.FetchAndAddAcquire(ting::s32(value)));
	}
	
	/**
	 * @brief Adds the value to this atomic variable and returns its initial value.
	 * It sets release memory semantics barrier.
	 * @param value - the value to add to this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::u32 FetchAndAddRelease(ting::u32 value)throw(){
		return ting::u32(this->v.FetchAndAddRelease(ting::s32(value)));
	}
	
	/**
	 * @brief Subtracts the value from this atomic variable and returns its initial value.
	 * It does not set any memory barrier.
	 * @param value - the value to subtract from this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::u32 FetchAndSubtract(ting::u32 value)throw(){
		return ting::u32(this->v.FetchAndAdd(-ting::s32(value)));
	}
	
	/**
	 * @brief Subtracts the value from this atomic variable and returns its initial value.
	 * It sets acquire memory semantics barrier.
	 * @param value - the value to subtract from this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::u32 FetchAndSubtractAcquire(ting::u32 value)throw(){
		return ting::u32(this->v.FetchAndAddAcquire(-ting::s32(value)));
	}
	
	/**
	 * @brief Subtracts the value from this atomic variable and returns its initial value.
	 * It sets release memory semantics barrier.
	 * @param value - the value to subtract from this atomic variable.
	 * @return initial value of this atomic variable.
	 */
	inline ting::u32 FetchAndSubtractRelease(ting::u32 value)throw(){
		return ting::u32(this->v.FetchAndAddRelease(-ting::s32(value)));
	}
	
	/**
	 * @brief Atomic compare and exchange operation.
	 * Compares the current value to the 'compareTo' value and if they are equal
	 * it will store the 'exchangeBy' value to the current value.
	 * It does not set any memory barrier.
	 * @param compareTo - the value to compare the current value to.
	 * @param exchangeBy - the value to store as the the current value in case the comparison will result in equals.
	 *                     Otherwise, the current value will remain untouched.
	 * @return Previous value.
	 */
	inline ting::u32 CompareAndExchange(ting::u32 compareTo, ting::u32 exchangeBy)throw(){
		return ting::u32(this->v.CompareAndExchange(ting::s32(compareTo), ting::s32(exchangeBy)));
	}

	/**
	 * @brief Atomic compare and exchange operation.
	 * Compares the current value to the 'compareTo' value and if they are equal
	 * it will store the 'exchangeBy' value to the current value.
	 * It sets acquire memory semantics barrier.
	 * @param compareTo - the value to compare the current value to.
	 * @param exchangeBy - the value to store as the the current value in case the comparison will result in equals.
	 *                     Otherwise, the current value will remain untouched.
	 * @return Previous value.
	 */
	inline ting::u32 CompareAndExchangeAcquire(ting::u32 compareTo, ting::u32 exchangeBy)throw(){
		return ting::u32(this->v.CompareAndExchangeAcquire(ting::s32(compareTo), ting::s32(exchangeBy)));
	}
	
	/**
	 * @brief Atomic compare and exchange operation.
	 * Compares the current value to the 'compareTo' value and if they are equal
	 * it will store the 'exchangeBy' value to the current value.
	 * It sets release memory semantics barrier.
	 * @param compareTo - the value to compare the current value to.
	 * @param exchangeBy - the value to store as the the current value in case the comparison will result in equals.
	 *                     Otherwise, the current value will remain untouched.
	 * @return Previous value.
	 */
	inline ting::u32 CompareAndExchangeRelease(ting::u32 compareTo, ting::u32 exchangeBy)throw(){
		return ting::u32(this->v.CompareAndExchangeRelease(ting::s32(compareTo), ting::s32(exchangeBy)));
	}
};

}//~namespace atomic
}//~namespace ting
