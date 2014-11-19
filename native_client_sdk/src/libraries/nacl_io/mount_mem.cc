// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nacl_io/mount_mem.h"

#include <errno.h>
#include <fcntl.h>

#include <string>

#include "nacl_io/mount.h"
#include "nacl_io/mount_node.h"
#include "nacl_io/mount_node_dir.h"
#include "nacl_io/mount_node_mem.h"
#include "nacl_io/osstat.h"
#include "nacl_io/osunistd.h"
#include "nacl_io/path.h"
#include "sdk_util/auto_lock.h"
#include "sdk_util/ref_object.h"

namespace nacl_io {

MountMem::MountMem() : root_(NULL) {}

Error MountMem::Init(int dev, StringMap_t& args, PepperInterface* ppapi) {
  Error error = Mount::Init(dev, args, ppapi);
  if (error)
    return error;

  root_.reset(new MountNodeDir(this));
  error = root_->Init(0);
  if (error) {
    root_.reset(NULL);
    return error;
  }
  return 0;
}

Error MountMem::FindNode(const Path& path,
                         int type,
                         ScopedMountNode* out_node) {
  out_node->reset(NULL);
  ScopedMountNode node = root_;

  // If there is no root there, we have an error.
  if (node == NULL)
    return ENOTDIR;

  // We are expecting an "absolute" path from this mount point.
  if (!path.IsAbsolute())
    return EINVAL;

  // Starting at the root, traverse the path parts.
  for (size_t index = 1; node && index < path.Size(); index++) {
    // If not a directory, then we have an error so return.
    if (!node->IsaDir())
      return ENOTDIR;

    // Find the child node
    Error error = node->FindChild(path.Part(index), &node);
    if (error)
      return error;
  }

  // If a directory is expected, but it's not a directory, then fail.
  if ((type & S_IFDIR) && !node->IsaDir())
    return ENOTDIR;

  // If a file is expected, but it's not a file, then fail.
  if ((type & S_IFREG) && node->IsaDir())
    return EISDIR;

  // We now have a valid object of the expected type, so return it.
  *out_node = node;
  return 0;
}

Error MountMem::Access(const Path& path, int a_mode) {
  ScopedMountNode node;
  Error error = FindNode(path, 0, &node);

  if (error)
    return error;

  int obj_mode = node->GetMode();
  if (((a_mode & R_OK) && !(obj_mode & S_IREAD)) ||
      ((a_mode & W_OK) && !(obj_mode & S_IWRITE)) ||
      ((a_mode & X_OK) && !(obj_mode & S_IEXEC))) {
    return EACCES;
  }

  return 0;
}

Error MountMem::Open(const Path& path, int open_flags,
                     ScopedMountNode* out_node) {
  out_node->reset(NULL);
  ScopedMountNode node;

  Error error = FindNode(path, 0, &node);
  if (error) {
    // If the node does not exist and we can't create it, fail
    if ((open_flags & O_CREAT) == 0)
      return ENOENT;

    // Now first find the parent directory to see if we can add it
    ScopedMountNode parent;
    error = FindNode(path.Parent(), S_IFDIR, &parent);
    if (error)
      return error;

    node.reset(new MountNodeMem(this));
    error = node->Init(open_flags);
    if (error)
      return error;

    error = parent->AddChild(path.Basename(), node);
    if (error)
      return error;

  } else {
    // Opening an existing file.

    // Directories can only be opened read-only.
    if (node->IsaDir() && (open_flags & 3) != O_RDONLY)
      return EISDIR;

    // If we were expected to create it exclusively, fail
    if (open_flags & O_EXCL)
      return EEXIST;

    if (open_flags & O_TRUNC)
      static_cast<MountNodeMem*>(node.get())->Resize(0);
  }


  *out_node = node;
  return 0;
}

Error MountMem::Mkdir(const Path& path, int mode) {
  // We expect a Mount "absolute" path
  if (!path.IsAbsolute())
    return ENOENT;

  // The root of the mount is already created by the mount
  if (path.Size() == 1)
    return EEXIST;

  ScopedMountNode parent;
  int error = FindNode(path.Parent(), S_IFDIR, &parent);
  if (error)
    return error;

  ScopedMountNode node;
  error = parent->FindChild(path.Basename(), &node);
  if (!error)
    return EEXIST;

  if (error != ENOENT)
    return error;

  // Allocate a node, with a RefCount of 1.  If added to the parent
  // it will get ref counted again.  In either case, release the
  // recount we have on exit.
  node.reset(new MountNodeDir(this));
  error = node->Init(0);
  if (error)
    return error;

  return parent->AddChild(path.Basename(), node);
}

Error MountMem::Unlink(const Path& path) {
  return RemoveInternal(path, REMOVE_FILE);
}

Error MountMem::Rmdir(const Path& path) {
  return RemoveInternal(path, REMOVE_DIR);
}

Error MountMem::Remove(const Path& path) {
  return RemoveInternal(path, REMOVE_ALL);
}

Error MountMem::Rename(const Path& src_path, const Path& target_path) {
  ScopedMountNode src_node;
  ScopedMountNode src_parent;
  ScopedMountNode target_node;
  ScopedMountNode target_parent;
  int error = FindNode(src_path, 0, &src_node);
  if (error)
    return error;

  // The source must exist
  error = FindNode(src_path.Parent(), S_IFDIR, &src_parent);
  if (error)
    return error;

  // The parent of the target must exist
  error = FindNode(target_path.Parent(), 0, &target_parent);
  if (error)
    return error;

  std::string target_name = target_path.Basename();

  // The target itself need not exist but if it does there are
  // certain restrictions
  error = FindNode(target_path, 0, &target_node);
  bool replacing_target = error == 0;
  if (replacing_target) {
    if (target_node->IsaDir()) {
      // If the target is a direcotry it must be empty
      if (target_node->ChildCount()) {
        return ENOTEMPTY;
      }

      if (src_node->IsaDir()) {
        // Replacing an existing directory.
        RemoveInternal(target_path, REMOVE_ALL);
      } else {
        // Renaming into an existing directory.
        target_name = src_path.Basename();
        target_parent = target_node;
      }
    } else {
      if (src_node->IsaDir())
        // Can't replace a file with a direcotory
        return EISDIR;

      // Replacing an existing file.
      target_parent->RemoveChild(target_path.Basename());
    }
  }

  // Perform that actual rename. Simply re-parent the original source node
  // onto its new parent node.
  error = src_parent->RemoveChild(src_path.Basename());
  if (error)
    return error;

  error = target_parent->AddChild(target_name, src_node);
  if (error) {
    // Re-parent the old target_node if we failed to add the new one.
    if (replacing_target)
      target_parent->AddChild(target_path.Basename(), target_node);
    // Re-parent the src_node
    target_parent->AddChild(target_path.Basename(), src_node);
    return error;
  }

  return 0;
}

Error MountMem::RemoveInternal(const Path& path, int remove_type) {
  bool dir_only = remove_type == REMOVE_DIR;
  bool file_only = remove_type == REMOVE_FILE;
  bool remove_dir = (remove_type & REMOVE_DIR) != 0;

  if (dir_only) {
    // We expect a Mount "absolute" path
    if (!path.IsAbsolute())
      return ENOENT;

    // The root of the mount is already created by the mount
    if (path.Size() == 1)
      return EEXIST;
  }

  ScopedMountNode parent;
  int error = FindNode(path.Parent(), S_IFDIR, &parent);
  if (error)
    return error;

  // Verify we find a child which is a directory.
  ScopedMountNode child;
  error = parent->FindChild(path.Basename(), &child);
  if (error)
    return error;

  if (dir_only && !child->IsaDir())
    return ENOTDIR;

  if (file_only && child->IsaDir())
    return EISDIR;

  if (remove_dir && child->ChildCount() > 0)
    return ENOTEMPTY;

  return parent->RemoveChild(path.Basename());
}

}  // namespace nacl_io
