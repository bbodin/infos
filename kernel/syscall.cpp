/* SPDX-License-Identifier: MIT */

/*
 * kernel/syscall.cpp
 * 
 * InfOS
 * Copyright (C) University of Edinburgh 2016.  All Rights Reserved.
 * 
 * Tom Spink <tspink@inf.ed.ac.uk>
 */
#include <infos/kernel/syscall.h>
#include <infos/kernel/kernel.h>
#include <infos/kernel/om.h>
#include <infos/kernel/thread.h>
#include <infos/kernel/process.h>
#include <infos/fs/file.h>
#include <infos/fs/directory.h>
#include <infos/util/string.h>
#include <arch/arch.h>

using namespace infos::kernel;
using namespace infos::fs;
using namespace infos::util;

// TODO: There is no userspace buffer checking done at all.  This really needs to be fixed...

ObjectHandle Syscall::sys_open(uintptr_t filename, uint32_t flags)
{
	File *f = sys.vfs().open((const char *)filename, flags);
	if (!f) {
		return KernelObject::Error;
	}

	return sys.object_manager().register_object(Thread::current(), f);
}

unsigned int Syscall::sys_close(ObjectHandle h)
{
	File *f = (File *)sys.object_manager().get_object_secure(Thread::current(), h);
	if (!f) {
		return -1;
	}
	
	f->close();
	return 0;
}

unsigned int Syscall::sys_read(ObjectHandle h, uintptr_t buffer, size_t size)
{
	File *f = (File *)sys.object_manager().get_object_secure(Thread::current(), h);
	if (!f) {
		return -1;
	}
	
	// TODO: Validate 'buffer' etc...
	return f->read((void *)buffer, size);
}

unsigned int Syscall::sys_write(ObjectHandle h, uintptr_t buffer, size_t size)
{
	File *f = (File *)sys.object_manager().get_object_secure(Thread::current(), h);
	if (!f) {
		return -1;
	}
	
	// TODO: Validate 'buffer' etc...
	return f->write((const void *)buffer, size);
}

ObjectHandle Syscall::sys_opendir(uintptr_t path, uint32_t flags)
{
	Directory *d = sys.vfs().opendir((const char *)path, flags);
	if (!d) return KernelObject::Error;
	
	return sys.object_manager().register_object(Thread::current(), d);
}

unsigned int Syscall::sys_closedir(ObjectHandle h)
{
	Directory *d = (Directory *)sys.object_manager().get_object_secure(Thread::current(), h);
	if (!d) {
		return -1;
	}
	
	d->close();
	return 0;
}

unsigned int Syscall::sys_readdir(ObjectHandle h, uintptr_t buffer)
{
	Directory *d = (Directory *)sys.object_manager().get_object_secure(Thread::current(), h);
	if (!d) {
		return 0;
	}
	
	DirectoryEntry de;
	if (d->read_entry(de)) {
		struct user_de {
			char name[64];
			unsigned int size;
			int flags;
		};
		
		user_de *ude = (user_de *)buffer;
		strncpy(ude->name, de.name.c_str(), 63);
		ude->flags = 0;
		ude->size = de.size;
		
		return 1;
	} else {
		return 0;
	}
}

void Syscall::sys_exit(unsigned int rc)
{
	Thread::current().owner().terminate(rc);
}

ObjectHandle Syscall::sys_exec(uintptr_t program, uintptr_t args)
{
	Process *p = sys.launch_process((const char *)program, (const char *)args);
	if (!p) {
		return KernelObject::Error;
	}
	
	return sys.object_manager().register_object(Thread::current(), p);
}

unsigned int Syscall::sys_wait_proc(ObjectHandle h)
{
	Process *p = (Process *)sys.object_manager().get_object_secure(Thread::current(), h);
	if (!p) {
		return -1;
	}
	
	sys.arch().disable_interrupts();
	while (!p->terminated()) {
		sys.arch().enable_interrupts();
		p->state_changed().wait();
	}

	return 0;
}

ObjectHandle Syscall::sys_create_thread(uintptr_t entry_point, uintptr_t arg)
{
	Thread& t = Thread::current().owner().create_thread(ThreadPrivilege::User, (Thread::thread_proc_t)entry_point);
	ObjectHandle h = sys.object_manager().register_object(Thread::current(), &t);
	
	virt_addr_t stack_addr = 0x7fff00000000;
	stack_addr += 0x2000 * (unsigned int)h;
	
	t.allocate_user_stack(stack_addr, 0x2000);
	//t.owner().vma().allocate_virt()
	t.add_entry_argument((void *)arg);
	t.start();
	
	return h;
}

unsigned int Syscall::sys_stop_thread(ObjectHandle h)
{
	Thread *t;
	if (h == (ObjectHandle)-1) {
		t = &Thread::current();
	} else {
		t = (Thread *)sys.object_manager().get_object_secure(Thread::current(), h);
	}
	
	if (!t) {
		return -1;
	}
	
	t->stop();
	
	return 0;
}

unsigned int Syscall::sys_join_thread(ObjectHandle h)
{
	Thread *t = (Thread *)sys.object_manager().get_object_secure(Thread::current(), h);
	if (!t) {
		return -1;
	}
	
	sys.arch().disable_interrupts();
	while (!t->stopped()) {
		sys.arch().enable_interrupts();
		t->state_changed().wait();
	}
	
	return 0;
}

unsigned long Syscall::sys_usleep(unsigned long us)
{
	sys.spin_delay(util::Microseconds(us));
	return us;
}
