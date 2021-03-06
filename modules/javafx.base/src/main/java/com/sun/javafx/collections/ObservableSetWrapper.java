/*
 * Copyright (c) 2010, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package com.sun.javafx.collections;

import java.util.Collection;
import java.util.Iterator;
import java.util.Set;

import javafx.beans.InvalidationListener;
import javafx.collections.ObservableSetBase;
import javafx.collections.SetChangeBuilder;
import javafx.collections.SetChangeListener;
import javafx.collections.SetComplexChangeListener;

/**
 * A Set wrapper class that implements observability.
 */
public class ObservableSetWrapper<E> extends ObservableSetBase<E>
{

    private final Set<E> backingSet;

    /**
     * Creates new instance of ObservableSet that wraps
     * the particular set specified by the parameter set.
     *
     * @param set the set being wrapped
     */
    public ObservableSetWrapper(Set<E> set) {
        this.backingSet = set;
    }

    /**
     * Returns number of elements contained in this set.
     *
     * @see java.util.Set in JDK API documentation
     * @return number of elements contained in the set
     */
    @Override
    public int size() {
        return backingSet.size();
    }

    /**
     * Returns true if this set contains no elements.
     *
     * @see java.util.Set in JDK API documentation
     * @return true if this set contains no elements
     */
    @Override
    public boolean isEmpty() {
        return backingSet.isEmpty();
    }

    /**
     * Returns true if this set contains specified element.
     *
     * @see java.util.Set in JDK API documentation
     * @param o an element that is being looked for
     * @return true if this set contains specified element
     */
    @Override
    public boolean contains(Object o) {
        return backingSet.contains(o);
    }

    /**
     * Returns an iterator over the elements in this set.
     * If the iterator's <code>remove()</code> method is called then the
     * registered observers are called as well.
     *
     * @see java.util.Set in JDK API documentation
     * @return an iterator over the elements in this set
     */
    @Override
    public Iterator iterator() {
        return new Iterator<E>() {

            private final Iterator<E> backingIt = backingSet.iterator();
            private E lastElement;

            @Override
            public boolean hasNext() {
                return backingIt.hasNext();
            }

            @Override
            public E next() {
                lastElement = backingIt.next();
                return lastElement;
            }

            @Override
            public void remove() {
                beginChange();
                backingIt.remove();
                nextRemove( lastElement );
                endChange();
            }
        };
    }

    /**
     * Returns an array containing all of the elements in this set.
     *
     * @see java.util.Set in JDK API documentation
     * @return an array containing all of the elements in this set
     */
    @Override
    public Object[] toArray() {
        return backingSet.toArray();
    }

    /**
     * Returns an array containing all of the elements in this set.
     * The runtime type of the returned array is that of the specified array.
     *
     * @see java.util.Set in JDK API documentation
     * @param a the array into which the elements of this set are to be stored, if it is big enough;
     * otherwise, a new array of the same runtime type is allocated
     * @return an array containing all of the elements in this set
     */
    @Override
    public <T> T[] toArray(T[] a) {
        return backingSet.toArray(a);
    }

    /**
     * Adds the specific element into this set and call all the
     * registered observers unless the set already contains the element.
     * Returns true in the case the element was added to the set.
     *
     * @see java.util.Set in JDK API documentation
     * @param o the element to be added to the set
     * @return true if the element was added
     */
    @Override
    public boolean add(E o) {
        beginChange();
        boolean ret = backingSet.add(o);
        if (ret) {
            nextAdd( o );
        }
        endChange();
        return ret;
    }

    /**
     * Removes the specific element from this set and call all the
     * registered observers if the set contained the element.
     * Returns true in the case the element was removed from the set.
     *
     * @see java.util.Set in JDK API documentation
     * @param o the element to be removed from the set
     * @return true if the element was removed
     */
    @Override
    public boolean remove(Object o) {
        beginChange();
        boolean ret = backingSet.remove(o);
        if (ret) {
            nextRemove( (E)o );
        }
        endChange();
        return ret;
    }

    /**
     * Test this set if it contains all the elements in the specified collection.
     * In such case returns true.
     *
     * @see java.util.Set in JDK API documentation
     * @param c collection to be checked for containment in this set
     * @return true if the set contains all the elements in the specified collection
     */
    @Override
    public boolean containsAll(Collection<?> c) {
        return backingSet.containsAll(c);
    }

    /**
     * Adds the elements from the specified collection.
     * Observers are called for each elements that was not already
     * present in the set.
     *
     * @see java.util.Set in JDK API documentation
     * @param c collection containing elements to be added to this set
     * @return true if this set changed as a result of the call
     */
    @Override
    public boolean addAll(Collection<?extends E> c) {
        beginChange();
        boolean ret = false;
        for (E element : c) {
            ret |= add(element);
        }
        endChange();
        return ret;
    }

    /**
     * Keeps only elements that are included the specified collection.
     * All other elements are removed. For each removed element all the
     * observers are called.
     *
     * @see java.util.Set in JDK API documentation
     * @param c collection containing elements to be kept in this set
     * @return true if this set changed as a result of the call
     */
    @Override
    public boolean retainAll(Collection<?> c) {
        return removeRetain(c, false);
    }

    /**
     * Removes all the elements that are contained in the specified
     * collection. Observers are called for each removed element.
     *
     * @see java.util.Set in JDK API documentation
     * @param c collection containing elements to be removed from this set
     * @return true if this set changed as a result of the call
     */
    @Override
    public boolean removeAll(Collection<?> c) {
        return removeRetain(c, true);
    }

    private boolean removeRetain(Collection<?> c, boolean remove) {
        beginChange();
        boolean removed = false;
        for (Iterator<E> i = backingSet.iterator(); i.hasNext();) {
            E element = i.next();
            if (remove == c.contains(element)) {
                removed = true;
                i.remove();
                nextRemove( element );
            }
        }
        endChange();
        return removed;
    }

    /**
     * Removes all the elements from this set. Observers are called
     * for each element.
     * @see java.util.Set in JDK API documentation
     */
    @Override
    public void clear() {
        beginChange();
        for (Iterator<E> i = backingSet.iterator(); i.hasNext(); ) {
            E element = i.next();
            i.remove();
            nextRemove( element );
        }
        endChange();
    }

    /**
     * Returns the String representation of the wrapped set.
     * @see java.lang.Object in JDK API documentation
     * @return the String representation of the wrapped set
     */
    @Override
    public String toString() {
        return backingSet.toString();
    }

    /**
     * Indicates whether some other object is "equal to" the wrapped set.
     * @see java.lang.Object in JDK API documentation
     * @param obj the reference object with which to compare
     * @return true if the wrapped is equal to the obj argument
     */
    @Override
    public boolean equals(Object obj) {
        return backingSet.equals(obj);
    }

    /**
     * Returns the hash code for the wrapped set.
     * @see java.lang.Object in JDK API documentation
     * @return the hash code for the wrapped set
     */
    @Override
    public int hashCode() {
        return backingSet.hashCode();
    }

}
