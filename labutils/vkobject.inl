namespace labutils
{
	// UniqueHandle<> implementation
	template< typename tHandle, typename tParent, DestroyFn<tParent,tHandle>& tDestroyFn >
	inline
	UniqueHandle<tHandle,tParent,tDestroyFn>::UniqueHandle( tParent aParent, tHandle aHandle ) noexcept
		: handle( aHandle )
		, mParent( aParent )
	{}

	template< typename tHandle, typename tParent, DestroyFn<tParent,tHandle>& tDestroyFn >
	inline 
	UniqueHandle<tHandle,tParent,tDestroyFn>::~UniqueHandle()
	{
		if( VK_NULL_HANDLE != handle )
		{
			assert( VK_NULL_HANDLE != mParent );
			tDestroyFn( mParent, handle, nullptr );
		}
	}

	template< typename tHandle, typename tParent, DestroyFn<tParent,tHandle>& tDestroyFn >
	inline 
	UniqueHandle<tHandle,tParent,tDestroyFn>::UniqueHandle( UniqueHandle&& aOther ) noexcept
		: handle( std::exchange( aOther.handle, VK_NULL_HANDLE ) )
		, mParent( std::exchange( aOther.mParent, VK_NULL_HANDLE ) )
	{}

	template< typename tHandle, typename tParent, DestroyFn<tParent,tHandle>& tDestroyFn >
	inline 
	UniqueHandle<tHandle,tParent,tDestroyFn>& UniqueHandle<tHandle,tParent,tDestroyFn>::operator=( UniqueHandle&& aOther ) noexcept
	{
		std::swap( handle, aOther.handle );
		std::swap( mParent, aOther.mParent );
		return *this;
	}
}
