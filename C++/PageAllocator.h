// The following license applies to all parts of this file.
/*************************************************
The MIT License

Copyright (c) 2012 Alex Christensen

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
THE SOFTWARE.
*************************************************/

//PageAllocator, by Alex Christensen
//
//Allocates 255 elements at a time, reducing the allocation time and the memory footprint.
//OverheadSize of 4 is usually ideal to maintain alignment for access speed, but it can be reduced to 1 to reduce memory.
//Deleting the PageAllocator frees each allocated element much faster than freeing them individually, such as deleting all nodes in the destructor of a TreeSet.
//This is intended for the operator new and operator delete for classes like tree nodes that are a constant size and often allocated.
//This allocator is not thread safe, so it must be protected by a mutex for multithread use
//OverheadSize must be nonzero, ElementSize must be nonzero

template <size_t ElementSize, size_t OverheadSize=4>
class PageAllocator
{
public:
	PageAllocator();
	~PageAllocator();
	void* allocate();
	void deallocate(void*);
private:

	template <size_t ElementSize, size_t OverheadSize>
	struct Page
	{

		// the element array, the first available index (unsigned char), and the total number of allocated elements (unsigned char)
		// 
		// a PageAllocator<sizeof(double)> would make an array starting like this with n being the index of the next element,
		// i the index of this element, _ an unused byte, f the first available index, and t the total number of allocated elements:
		// n_______i___n_______i___...n_______i___ft
		// when it is full, the array would look like this with d being a byte used for a double:
		// ddddddddi___ddddddddi___...ddddddddi___ft
		
		unsigned char elements[255*(ElementSize+OverheadSize)+2];

		//two pointers for doubly linked lists of allocated pages
		Page<ElementSize,OverheadSize>* nextPage;
		Page<ElementSize,OverheadSize>* prevPage;

		Page()
		{
			nextPage=0;
			prevPage=0;
			elements[255*(ElementSize+OverheadSize)+0]=(unsigned char)0;//first available index
			elements[255*(ElementSize+OverheadSize)+1]=(unsigned char)0;//number of allocated elements

			//set up the indices and the indices of the following available element (like a singly linked list of indices)
			for(unsigned char i=0;i<255;i++)
			{
				elements[i*(ElementSize+OverheadSize)+ElementSize]=i;//index of the element
				elements[i*(ElementSize+OverheadSize)]=i+1;//index of the following available element
			}
		}
	};

	//keep a doubly linked list of full pages and a doubly linked list of pages with available allocation slots
	Page<ElementSize, OverheadSize>* fullPages;
	Page<ElementSize, OverheadSize>* notFullPages;//there is always at least one notFullPage, allocation always happens from the first notFullPage
};

template <size_t ElementSize,size_t OverheadSize>
PageAllocator<ElementSize,OverheadSize>::PageAllocator()
{
	notFullPages=new Page<ElementSize,OverheadSize>();
	fullPages=0;
}

template <size_t ElementSize, size_t OverheadSize>
PageAllocator<ElementSize,OverheadSize>::~PageAllocator()
{
	//delete each allocated page from the 2 doubly linked lists
	Page<ElementSize,OverheadSize>* pageToDelete=notFullPages;
	while(pageToDelete)
	{
		Page<ElementSize,OverheadSize>* thisPage=pageToDelete;
		pageToDelete=pageToDelete->nextPage;
		delete thisPage;
	}
	pageToDelete=fullPages;
	while(pageToDelete)
	{
		Page<ElementSize,OverheadSize>* thisPage=pageToDelete;
		pageToDelete=pageToDelete->nextPage;
		delete thisPage;
	}
}

template <size_t ElementSize, size_t OverheadSize>
void* PageAllocator<ElementSize,OverheadSize>::allocate()
{
	//allocate from the beginning of the singly linked list of indices
	unsigned char* pFirstAvailableElementIndex=notFullPages->elements+255*(ElementSize+OverheadSize)+0;
	unsigned char* pNumAllocatedElements      =notFullPages->elements+255*(ElementSize+OverheadSize)+1;
	unsigned char* allocatedElement=(notFullPages->elements)+(ElementSize+OverheadSize)*(*pFirstAvailableElementIndex);
	*pFirstAvailableElementIndex=*allocatedElement;

	//increment the number of allocated elements and remove the page from notFullPages and insert into fullPages if it's full
	if(++(*pNumAllocatedElements)==(unsigned char)255)//if it's full
	{
		Page<ElementSize,OverheadSize>* page=notFullPages;

		notFullPages=page->nextPage;
		if(notFullPages)
			notFullPages->prevPage=0;//used to be page
		//page->prevPage=0;//it's already 0 because it was the first notFullPage
		page->nextPage=fullPages;
		if(fullPages)
			fullPages->prevPage=page;//used to be 0
		fullPages=page;

		//allocate another notFullPage if there isn't one
		if(notFullPages==0)
			notFullPages=new Page<ElementSize,OverheadSize>();
	}
	return allocatedElement;
}

template <size_t ElementSize,size_t OverheadSize>
void PageAllocator<ElementSize,OverheadSize>::deallocate(void* element)
{
	//put this element at the beginning of the singly linked list of indices and decrement the number of allocated elements
	unsigned char index=((unsigned char*)element)[ElementSize];
	unsigned char* pFirstAvailableElementIndex=((unsigned char*)element)+(255-index)*(ElementSize+OverheadSize)+0;
	unsigned char* pNumAllocatedElements      =((unsigned char*)element)+(255-index)*(ElementSize+OverheadSize)+1;
	*((unsigned char*)element)=*pFirstAvailableElementIndex;
	*pFirstAvailableElementIndex=index;
	(*pNumAllocatedElements)--;

	if(*pNumAllocatedElements==0)
	{
		//remove the page from the notFullPages doubly linked list and delete the page if it's not the only notFullPage
		Page<ElementSize,OverheadSize>* page=(Page<ElementSize,OverheadSize>*)(((unsigned char*)element)-index*(ElementSize+OverheadSize));
		if(page->prevPage||page->nextPage)
		{
			if(page->nextPage)
				page->nextPage->prevPage=page->prevPage;
			if(page->prevPage)
				page->prevPage->nextPage=page->nextPage;
			else
				notFullPages=page->nextPage;
			delete page;
		}
	}
	else if(*pNumAllocatedElements==254)
	{
		//remove the page from the fullPages and insert it into the notFullPages if it was full (but isn't anymore)
		Page<ElementSize,OverheadSize>* page=(Page<ElementSize,OverheadSize>*)(((unsigned char*)element)-index*(ElementSize+OverheadSize));
		if(page->nextPage)
			page->nextPage->prevPage=page->prevPage;
		if(page->prevPage)
			page->prevPage->nextPage=page->nextPage;
		else
			fullPages=page->nextPage;

		page->nextPage=notFullPages;
		page->prevPage=0;
		notFullPages->prevPage=page;
		notFullPages=page;
	}
}
